#include "gfx/VulkanContext.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ostream>
#include <stdexcept>
#include <string>

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include "spirv/CloudPixelShader.h"
#include "spirv/InterfacePixelShader.h"
#include "spirv/InstancedDepthVertexShader.h"
#include "spirv/InstancedShadowVertexShader.h"
#include "spirv/InstancedWorldVertexShader.h"
#include "spirv/InterfaceVertexShader.h"
#include "spirv/PostCopyShader.h"
#include "spirv/PostVertexShader.h"
#include "spirv/DepthVertexShader.h"
#include "spirv/ShadowVertexShader.h"
#include "spirv/ReflectiveShadowVertexShader.h"
#include "spirv/ReflectiveShadowPixelShader.h"
#include "spirv/SkyPixelShader.h"
#include "spirv/SmaaBlendShader.h"
#include "spirv/SmaaEdgesShader.h"
#include "spirv/SmaaWeightsShader.h"
#include "spirv/TemporalResolveShader.h"
#include "spirv/TexturedPixelShader.h"
#include "spirv/TexturedVertexShader.h"
#include "spirv/WaterPixelShader.h"
#include "spirv/WaterVertexShader.h"
#include "spirv/WorldPixelShader.h"
#include "spirv/WorldVertexShader.h"
#include "../../thirdparty/smaa/AreaTex.h"
#include "../../thirdparty/smaa/SearchTex.h"

namespace gfx {
namespace {

constexpr VkFormat ColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkFormat DepthFormat = VK_FORMAT_D32_SFLOAT;
constexpr VkDeviceSize UniformCapacity = 1024;
constexpr VkDeviceSize StreamPageSize = 2 * 1024 * 1024;
// drawInstanced's records: the world matrix's 16 floats, the material colour and the alpha (VulkanSpriteInstance).
constexpr uint32_t InstanceRecordSize = 84;
constexpr uint32_t ImageSetsPerPool = 2048;
constexpr uint32_t BufferSetsPerPool = 64;
constexpr std::array<uint32_t, 4> ImageBindings = {0, 1, 3, 4};

void check(VkResult result, const char *operation)
{
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed (" + std::to_string(result) + ")");
}

bool sameSize(VkExtent2D a, VkExtent2D b)
{
    return a.width == b.width && a.height == b.height;
}

bool hasSwapchain(VkPhysicalDevice device)
{
    uint32_t count = 0;
    check(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr), "Enumerate device extensions");
    std::vector<VkExtensionProperties> properties(count);
    check(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, properties.data()), "Read device extensions");
    return std::any_of(properties.begin(), properties.end(), [](const auto &extension) {
        return std::strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
    });
}

VkImageView createView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspect)
{
    VkImageViewCreateInfo info {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    info.image = image;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = format;
    info.subresourceRange = {aspect, 0, 1, 0, 1};
    VkImageView view;
    check(vkCreateImageView(device, &info, nullptr, &view), "Create image view");
    return view;
}

VkSampleCountFlagBits sampleCount(VkPhysicalDevice device, VkFormat color, unsigned requested)
{
    VkImageFormatProperties colorProperties = {};
    VkImageFormatProperties depthProperties = {};
    check(vkGetPhysicalDeviceImageFormatProperties(device, color, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                                                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 0, &colorProperties),
          "Query color attachment format");
    check(vkGetPhysicalDeviceImageFormatProperties(device, DepthFormat, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                                                   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 0, &depthProperties),
          "Query depth attachment format");
    const VkSampleCountFlags supported = colorProperties.sampleCounts & depthProperties.sampleCounts;
    for (auto count : {VK_SAMPLE_COUNT_64_BIT, VK_SAMPLE_COUNT_32_BIT, VK_SAMPLE_COUNT_16_BIT, VK_SAMPLE_COUNT_8_BIT,
                       VK_SAMPLE_COUNT_4_BIT, VK_SAMPLE_COUNT_2_BIT}) {
        if (count <= requested && (supported & count))
            return count;
    }
    return VK_SAMPLE_COUNT_1_BIT;
}

}

struct VulkanBuffer {
    VmaAllocator allocator;
    VkBuffer handle = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    void *mapped = nullptr;

    ~VulkanBuffer()
    {
        if (handle)
            vmaDestroyBuffer(allocator, handle, allocation);
    }
};

struct VulkanImage {
    VkDevice device;
    VmaAllocator allocator = nullptr;
    VkImage handle = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VkExtent2D size = {};
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 access = VK_ACCESS_2_NONE;
    BufferPtr pendingUpload;

    ~VulkanImage()
    {
        if (view)
            vkDestroyImageView(device, view, nullptr);
        if (allocation)
            vmaDestroyImage(allocator, handle, allocation);
    }
};

struct VulkanContext::StreamPage {
    BufferPtr buffer;
    VkDeviceSize capacity;
    VkDeviceSize used = 0;
    VkDescriptorSet uniforms = VK_NULL_HANDLE;
};

struct VulkanContext::Frame {
    VkCommandPool commands = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkFence complete = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;
    ImagePtr msaaColor;
    ImagePtr depth;
    std::vector<StreamPage> pages;
    size_t pageIndex = 0;
    std::vector<VkDescriptorPool> pools;
    size_t poolIndex = 0;
    std::map<std::pair<std::array<VkImageView, ImageBindings.size()>, std::array<VkSampler, 2>>, VkDescriptorSet> imageSets;
    std::vector<std::shared_ptr<void>> inUse;
    VkPipeline boundPipeline = VK_NULL_HANDLE;
    // The GPU's start and end of the frame's commands; timed once a submitted frame wrote them.
    VkQueryPool timestamps = VK_NULL_HANDLE;
    bool timed = false;
};

VulkanContext::VulkanContext() = default;

VulkanContext::~VulkanContext()
{
    if (m_device) {
        vkDeviceWaitIdle(m_device);
        m_textures = {};
        m_emptyTexture.reset();
        m_vertex = {};
        m_index = {};
        destroySwapchain();
        for (auto &frame : m_frames) {
            if (!frame)
                continue;
            for (VkDescriptorPool pool : frame->pools)
                vkDestroyDescriptorPool(m_device, pool, nullptr);
            vkDestroySemaphore(m_device, frame->acquired, nullptr);
            vkDestroyFence(m_device, frame->complete, nullptr);
            vkDestroyCommandPool(m_device, frame->commands, nullptr);
            if (frame->timestamps)
                vkDestroyQueryPool(m_device, frame->timestamps, nullptr);
            frame.reset();
        }
        for (const auto &entry : m_pipelines)
            vkDestroyPipeline(m_device, entry.second, nullptr);
        for (VkShaderModule shader : m_vertexShaders)
            vkDestroyShaderModule(m_device, shader, nullptr);
        for (VkShaderModule shader : m_fragmentShaders)
            if (shader)
                vkDestroyShaderModule(m_device, shader, nullptr);
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(m_device, m_textureLayout, nullptr);
        vkDestroyDescriptorSetLayout(m_device, m_uniformLayout, nullptr);
        for (const auto &entry : m_samplers)
            vkDestroySampler(m_device, entry.second, nullptr);
        vkDestroySampler(m_device, m_sampler, nullptr);
        vkDestroySampler(m_device, m_pointSampler, nullptr);
        if (m_allocator)
            vmaDestroyAllocator(m_allocator);
        vkDestroyDevice(m_device, nullptr);
    }
    if (m_surface)
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    if (m_instance)
        vkDestroyInstance(m_instance, nullptr);
}

void VulkanContext::initialize(const char *surfaceExtension, const SurfaceFactory &createSurface,
                               std::ostream &startupLog)
{
    check(volkInitialize(), "Load the Vulkan runtime");
    if (volkGetInstanceVersion() < VK_API_VERSION_1_3)
        throw std::runtime_error("Cube World requires a Vulkan 1.3 driver.");

    VkApplicationInfo application {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.pApplicationName = "Cube World";
    application.apiVersion = VK_API_VERSION_1_3;

    const char *extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, surfaceExtension};
    VkInstanceCreateInfo instance {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instance.pApplicationInfo = &application;
    instance.enabledExtensionCount = 2;
    instance.ppEnabledExtensionNames = extensions;
    check(vkCreateInstance(&instance, nullptr, &m_instance), "Create Vulkan instance");
    volkLoadInstance(m_instance);

    check(createSurface(m_instance, &m_surface), "Create Vulkan window surface");
    createDevice();
    startupLog << "Vulkan 1.3 device: " << m_properties.deviceName << std::endl;

    VmaVulkanFunctions functions = {};
    functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    VmaAllocatorCreateInfo allocator = {};
    allocator.physicalDevice = m_physicalDevice;
    allocator.device = m_device;
    allocator.instance = m_instance;
    allocator.vulkanApiVersion = VK_API_VERSION_1_3;
    allocator.pVulkanFunctions = &functions;
    check(vmaCreateAllocator(&allocator, &m_allocator), "Create Vulkan allocator");
    createLayouts();
    createShaders();

    for (auto &frame : m_frames) {
        frame = std::make_unique<Frame>();
        VkCommandPoolCreateInfo pool {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool.queueFamilyIndex = m_queueFamily;
        check(vkCreateCommandPool(m_device, &pool, nullptr, &frame->commands), "Create frame command pool");
        VkCommandBufferAllocateInfo commands {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        commands.commandPool = frame->commands;
        commands.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commands.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(m_device, &commands, &frame->command), "Allocate frame command buffer");
        VkFenceCreateInfo fence {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        check(vkCreateFence(m_device, &fence, nullptr, &frame->complete), "Create frame fence");
        VkSemaphoreCreateInfo semaphore {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        check(vkCreateSemaphore(m_device, &semaphore, nullptr, &frame->acquired), "Create acquire semaphore");
        if (m_timestampBits) {
            VkQueryPoolCreateInfo timestamps {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            timestamps.queryType = VK_QUERY_TYPE_TIMESTAMP;
            timestamps.queryCount = 2;
            check(vkCreateQueryPool(m_device, &timestamps, nullptr, &frame->timestamps), "Create frame timestamps");
        }
    }
    const uint32_t transparentBlack = 0;
    m_emptyTexture = createTexture(1, 1, &transparentBlack);
}

void VulkanContext::createDevice()
{
    uint32_t count = 0;
    check(vkEnumeratePhysicalDevices(m_instance, &count, nullptr), "Enumerate Vulkan adapters");
    std::vector<VkPhysicalDevice> devices(count);
    check(vkEnumeratePhysicalDevices(m_instance, &count, devices.data()), "Read Vulkan adapters");
    VkBool32 maintenance4 = VK_FALSE;

    for (VkPhysicalDevice candidate : devices) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(candidate, &properties);
        if (properties.apiVersion < VK_API_VERSION_1_3 || !hasSwapchain(candidate))
            continue;
        if (m_physicalDevice && properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            continue;
        VkPhysicalDeviceVulkan13Features features13 {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 features {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &features13};
        vkGetPhysicalDeviceFeatures2(candidate, &features);
        if (!features13.dynamicRendering || !features13.synchronization2)
            continue;

        uint32_t queueCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, nullptr);
        std::vector<VkQueueFamilyProperties> queues(queueCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, queues.data());
        for (uint32_t i = 0; i < queueCount; ++i) {
            VkBool32 present = VK_FALSE;
            check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, m_surface, &present),
                  "Query surface presentation support");
            if ((queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                m_physicalDevice = candidate;
                m_properties = properties;
                m_queueFamily = i;
                m_timestampBits = queues[i].timestampValidBits;
                maintenance4 = features13.maintenance4;
                break;
            }
        }
        if (m_physicalDevice && m_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            break;
    }
    if (!m_physicalDevice)
        throw std::runtime_error("No Vulkan 1.3 adapter supports graphics and presentation for this window.");

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue.queueFamilyIndex = m_queueFamily;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;

    VkPhysicalDeviceVulkan13Features features {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features.dynamicRendering = VK_TRUE;
    features.synchronization2 = VK_TRUE;
    features.maintenance4 = maintenance4;
    const char *swapchain = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo device {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device.pNext = &features;
    device.queueCreateInfoCount = 1;
    device.pQueueCreateInfos = &queue;
    device.enabledExtensionCount = 1;
    device.ppEnabledExtensionNames = &swapchain;
    check(vkCreateDevice(m_physicalDevice, &device, nullptr, &m_device), "Create Vulkan device");
    volkLoadDevice(m_device);
    vkGetDeviceQueue(m_device, m_queueFamily, 0, &m_queue);
}

void VulkanContext::createLayouts()
{
    VkSamplerCreateInfo sampler {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    check(vkCreateSampler(m_device, &sampler, nullptr, &m_sampler), "Create texture sampler");
    sampler.magFilter = VK_FILTER_NEAREST;
    sampler.minFilter = VK_FILTER_NEAREST;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    check(vkCreateSampler(m_device, &sampler, nullptr, &m_pointSampler), "Create point sampler");

    VkDescriptorSetLayoutBinding uniform = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1,
                                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT};
    VkDescriptorSetLayoutCreateInfo layout {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout.bindingCount = 1;
    layout.pBindings = &uniform;
    check(vkCreateDescriptorSetLayout(m_device, &layout, nullptr, &m_uniformLayout), "Create uniform layout");

    const VkDescriptorSetLayoutBinding textures[] = {
        {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {2, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &m_sampler},
        {3, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {4, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {5, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &m_pointSampler},
        {6, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {7, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
    };
    layout.bindingCount = static_cast<uint32_t>(std::size(textures));
    layout.pBindings = textures;
    check(vkCreateDescriptorSetLayout(m_device, &layout, nullptr, &m_textureLayout), "Create texture layout");

    const VkDescriptorSetLayout sets[] = {m_uniformLayout, m_textureLayout};
    VkPipelineLayoutCreateInfo pipeline {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipeline.setLayoutCount = 2;
    pipeline.pSetLayouts = sets;
    check(vkCreatePipelineLayout(m_device, &pipeline, nullptr, &m_pipelineLayout), "Create pipeline layout");
}

void VulkanContext::setTextureSampler(unsigned slot, VkFilter min, VkFilter mag, VkSamplerAddressMode u,
                                      VkSamplerAddressMode v)
{
    const auto key = std::make_tuple(min, mag, u, v);
    auto it = m_samplers.find(key);
    if (it == m_samplers.end()) {
        VkSamplerCreateInfo info {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        info.minFilter = min;
        info.magFilter = mag;
        info.addressModeU = u;
        info.addressModeV = v;
        info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        VkSampler sampler;
        check(vkCreateSampler(m_device, &info, nullptr, &sampler), "Create texture sampler");
        it = m_samplers.emplace(key, sampler).first;
    }
    m_textureSamplers.at(slot) = it->second;
}

void VulkanContext::createShaders()
{
    const auto create = [this](const uint32_t *words, size_t bytes) {
        VkShaderModuleCreateInfo info {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = bytes;
        info.pCode = words;
        VkShaderModule shader;
        check(vkCreateShaderModule(m_device, &info, nullptr, &shader), "Create SPIR-V shader module");
        return shader;
    };
    m_vertexShaders[0] = create(WorldVertexShader, sizeof(WorldVertexShader));
    m_vertexShaders[1] = create(TexturedVertexShader, sizeof(TexturedVertexShader));
    m_vertexShaders[2] = create(InterfaceVertexShader, sizeof(InterfaceVertexShader));
    m_vertexShaders[3] = create(ShadowVertexShader, sizeof(ShadowVertexShader));
    m_vertexShaders[4] = create(PostVertexShader, sizeof(PostVertexShader));
    m_vertexShaders[5] = create(ReflectiveShadowVertexShader, sizeof(ReflectiveShadowVertexShader));
    m_vertexShaders[6] = create(WaterVertexShader, sizeof(WaterVertexShader));
    m_vertexShaders[7] = create(InstancedWorldVertexShader, sizeof(InstancedWorldVertexShader));
    m_vertexShaders[8] = create(InstancedShadowVertexShader, sizeof(InstancedShadowVertexShader));
    m_vertexShaders[9] = create(DepthVertexShader, sizeof(DepthVertexShader));
    m_vertexShaders[10] = create(InstancedDepthVertexShader, sizeof(InstancedDepthVertexShader));
    m_fragmentShaders[0] = create(WorldPixelShader, sizeof(WorldPixelShader));
    m_fragmentShaders[1] = create(SkyPixelShader, sizeof(SkyPixelShader));
    m_fragmentShaders[2] = create(WaterPixelShader, sizeof(WaterPixelShader));
    m_fragmentShaders[3] = create(CloudPixelShader, sizeof(CloudPixelShader));
    m_fragmentShaders[4] = create(TexturedPixelShader, sizeof(TexturedPixelShader));
    m_fragmentShaders[5] = create(InterfacePixelShader, sizeof(InterfacePixelShader));
    m_fragmentShaders[size_t(VulkanProgram::ReflectiveShadow)] =
        create(ReflectiveShadowPixelShader, sizeof(ReflectiveShadowPixelShader));
    m_fragmentShaders[size_t(VulkanProgram::PostCopy)] = create(PostCopyShader, sizeof(PostCopyShader));
    m_fragmentShaders[size_t(VulkanProgram::TemporalResolve)] =
        create(TemporalResolveShader, sizeof(TemporalResolveShader));
    m_fragmentShaders[size_t(VulkanProgram::SmaaEdges)] = create(SmaaEdgesShader, sizeof(SmaaEdgesShader));
    m_fragmentShaders[size_t(VulkanProgram::SmaaWeights)] = create(SmaaWeightsShader, sizeof(SmaaWeightsShader));
    m_fragmentShaders[size_t(VulkanProgram::SmaaBlend)] = create(SmaaBlendShader, sizeof(SmaaBlendShader));
}

void VulkanContext::waitIdle() const
{
    if (m_device)
        check(vkDeviceWaitIdle(m_device), "Wait for the Vulkan device");
}

// Android's compositor rotates the image to the display's orientation. It reports that as a suboptimal swapchain on
// every frame, which is no reason to rebuild it.
#ifdef __ANDROID__
constexpr bool CompositorRotates = true;
#else
constexpr bool CompositorRotates = false;
#endif

void VulkanContext::releaseSurface()
{
    if (!m_surface)
        return;
    waitIdle();
    destroySwapchain();
    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    m_surface = VK_NULL_HANDLE;
}

void VulkanContext::replaceSurface(const SurfaceFactory &createSurface)
{
    releaseSurface();
    check(createSurface(m_instance, &m_surface), "Create Vulkan window surface");
}

void VulkanContext::destroySwapchain()
{
    m_targets.clear();
    m_post = {};
    m_waterColor.reset();
    m_waterDepth.reset();
    for (auto &frame : m_frames) {
        if (frame) {
            frame->inUse.clear();
            frame->depth.reset();
            frame->msaaColor.reset();
        }
    }
    for (VkSemaphore semaphore : m_renderFinished)
        vkDestroySemaphore(m_device, semaphore, nullptr);
    m_renderFinished.clear();
    m_swapImages.clear();
    if (m_swapchain)
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
    m_swapchain = VK_NULL_HANDLE;
}

bool VulkanContext::recreateSwapchain(VkExtent2D clientSize, bool vsync, unsigned requestedSamples)
{
    VkSurfaceCapabilitiesKHR capabilities;
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &capabilities),
          "Query window surface capabilities");
    VkExtent2D size = capabilities.currentExtent;
    if (size.width == UINT32_MAX) {
        size.width = std::clamp(clientSize.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        size.height =
            std::clamp(clientSize.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }
    if (!size.width || !size.height)
        return false;
    constexpr std::array<VkCompositeAlphaFlagBitsKHR, 4> AlphaModes = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR};
    const auto compositeAlpha = std::find_if(AlphaModes.begin(), AlphaModes.end(),
                                             [&](auto mode) { return capabilities.supportedCompositeAlpha & mode; });
    if (compositeAlpha == AlphaModes.end())
        throw std::runtime_error("The Vulkan surface does not offer a composite alpha mode.");

    uint32_t count = 0;
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &count, nullptr), "Count surface formats");
    std::vector<VkSurfaceFormatKHR> formats(count);
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &count, formats.data()),
          "Read surface formats");
    auto format = std::find_if(formats.begin(), formats.end(), [](VkSurfaceFormatKHR candidate) {
        return candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR &&
               (candidate.format == ColorFormat || candidate.format == VK_FORMAT_B8G8R8A8_UNORM);
    });
    if (format == formats.end())
        throw std::runtime_error("The Vulkan surface does not offer an 8-bit UNORM color format.");

    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    if (!vsync) {
        check(vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &count, nullptr),
              "Count present modes");
        std::vector<VkPresentModeKHR> modes(count);
        check(vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &count, modes.data()),
              "Read present modes");
        for (auto preferred : {VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR}) {
            if (std::find(modes.begin(), modes.end(), preferred) != modes.end()) {
                mode = preferred;
                break;
            }
        }
    }
    const bool swapchainTransferSource = (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    const auto samples = sampleCount(m_physicalDevice, format->format, requestedSamples);
    uint32_t images = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount)
        images = std::min(images, capabilities.maxImageCount);

    waitIdle();
    destroySwapchain();
    VkSwapchainCreateInfoKHR info {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = m_surface;
    info.minImageCount = images;
    info.imageFormat = format->format;
    info.imageColorSpace = format->colorSpace;
    info.imageExtent = size;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (swapchainTransferSource)
        info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = CompositorRotates ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : capabilities.currentTransform;
    info.compositeAlpha = *compositeAlpha;
    info.presentMode = mode;
    info.clipped = VK_TRUE;
    check(vkCreateSwapchainKHR(m_device, &info, nullptr, &m_swapchain), "Create Vulkan swapchain");
    check(vkGetSwapchainImagesKHR(m_device, m_swapchain, &count, nullptr), "Count swapchain images");
    std::vector<VkImage> swapImages(count);
    check(vkGetSwapchainImagesKHR(m_device, m_swapchain, &count, swapImages.data()), "Read swapchain images");
    m_renderFinished.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        auto image = std::make_shared<VulkanImage>();
        image->device = m_device;
        image->handle = swapImages[i];
        image->size = size;
        image->format = format->format;
        image->view = createView(m_device, image->handle, image->format, image->aspect);
        m_swapImages.push_back(image);
        // Present waits on a semaphore tied to the image so a frame fence cannot recycle it early.
        VkSemaphoreCreateInfo semaphore {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        check(vkCreateSemaphore(m_device, &semaphore, nullptr, &m_renderFinished[i]), "Create present semaphore");
    }
    const VkImageUsageFlags depthUsage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                         (samples == VK_SAMPLE_COUNT_1_BIT ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
    for (auto &frame : m_frames) {
        if (samples != VK_SAMPLE_COUNT_1_BIT)
            frame->msaaColor = makeImage(size, format->format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                         VK_IMAGE_ASPECT_COLOR_BIT, samples);
        frame->depth = makeImage(size, DepthFormat, depthUsage, VK_IMAGE_ASPECT_DEPTH_BIT, samples);
    }
    m_extent = size;
    createPostTargets(format->format, !swapchainTransferSource);
    m_clientSize = clientSize;
    m_samples = samples;
    m_requestedSamples = requestedSamples;
    m_vsync = vsync;
    m_recreate = false;
    return true;
}

VkExtent2D VulkanContext::beginFrame(VkExtent2D clientSize, bool vsync, unsigned requestedSamples, bool temporalAA,
                                     bool spatialAA, std::array<float, 4> clearColor)
{
    if (!m_surface || !clientSize.width || !clientSize.height)
        return {};
    if (temporalAA != m_temporalAA || spatialAA != m_spatialAA) {
        m_temporalAA = temporalAA;
        m_spatialAA = spatialAA;
        m_recreate = true;
    }
    if (temporalAA || spatialAA)
        requestedSamples = 1;
    if (!m_swapchain || m_recreate || !sameSize(clientSize, m_clientSize) || vsync != m_vsync ||
        requestedSamples != m_requestedSamples) {
        if (!recreateSwapchain(clientSize, vsync, requestedSamples))
            return {};
    }

    Frame &frame = *m_frames[m_frameIndex];
    const auto waitStart = std::chrono::steady_clock::now();
    check(vkWaitForFences(m_device, 1, &frame.complete, VK_TRUE, UINT64_MAX), "Wait for rendering");
    const VkResult acquire =
        vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, frame.acquired, VK_NULL_HANDLE, &m_imageIndex);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        m_recreate = true;
        return {};
    }
    if (acquire == VK_SUBOPTIMAL_KHR)
        m_recreate = !CompositorRotates;
    else
        check(acquire, "Acquire swapchain image");
    m_frameStats.waitMilliseconds =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - waitStart).count();
    // This slot's last frame has finished, so its timestamps are ready.
    if (frame.timed) {
        uint64_t times[2];
        if (vkGetQueryPoolResults(m_device, frame.timestamps, 0, 2, sizeof(times), times, sizeof(times[0]),
                                  VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
            const uint64_t mask = m_timestampBits < 64 ? (uint64_t(1) << m_timestampBits) - 1 : ~uint64_t(0);
            m_frameStats.gpuMilliseconds =
                float(double((times[1] - times[0]) & mask) * m_properties.limits.timestampPeriod * 1e-6);
        }
        frame.timed = false;
    }

    check(vkResetCommandPool(m_device, frame.commands, 0), "Reset frame commands");
    for (VkDescriptorPool pool : frame.pools)
        check(vkResetDescriptorPool(m_device, pool, 0), "Reset frame descriptors");
    frame.inUse.clear();
    frame.imageSets.clear();
    frame.poolIndex = 0;
    frame.pageIndex = 0;
    frame.boundPipeline = VK_NULL_HANDLE;
    for (auto &page : frame.pages) {
        page.used = 0;
        page.uniforms = VK_NULL_HANDLE;
    }
    m_frame = &frame;
    m_uniforms = nullptr;
    m_vertex = {};
    m_index = {};
    VkCommandBufferBeginInfo command {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    command.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(frame.command, &command), "Begin frame commands");
    if (frame.timestamps) {
        vkCmdResetQueryPool(frame.command, frame.timestamps, 0, 2);
        vkCmdWriteTimestamp2(frame.command, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, frame.timestamps, 0);
    }
    m_draws = 0;
    m_triangles = 0;
    const auto &scene = m_post.scene ? m_post.scene : m_swapImages[m_imageIndex];
    m_post.resolved = false;
    if (m_temporalAA || m_spatialAA)
        m_targets.push_back({scene, m_post.depth[m_frameIndex], {}});
    else
        m_targets.push_back(frame.msaaColor ? Target {frame.msaaColor, frame.depth, scene}
                                            : Target {scene, frame.depth, {}});
    beginRendering(true, clearColor);
    return m_extent;
}

bool VulkanContext::present()
{
    const auto presentStart = std::chrono::steady_clock::now();
    finishScene();
    endRendering();
    transition(m_swapImages[m_imageIndex], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);
    m_targets.clear();
    Frame &frame = *m_frame;
    if (frame.timestamps) {
        vkCmdWriteTimestamp2(frame.command, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, frame.timestamps, 1);
        frame.timed = true;
    }
    check(vkEndCommandBuffer(frame.command), "End frame commands");
    for (const auto &page : frame.pages) {
        if (page.used)
            check(vmaFlushAllocation(m_allocator, page.buffer->allocation, 0, page.used), "Flush frame data");
    }

    VkSemaphoreSubmitInfo acquired {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    acquired.semaphore = frame.acquired;
    acquired.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphoreSubmitInfo rendered {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    rendered.semaphore = m_renderFinished[m_imageIndex];
    rendered.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkCommandBufferSubmitInfo command {VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    command.commandBuffer = frame.command;
    VkSubmitInfo2 submit {VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &acquired;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &rendered;
    check(vkResetFences(m_device, 1, &frame.complete), "Reset frame fence");
    check(vkQueueSubmit2(m_queue, 1, &submit, frame.complete), "Submit Vulkan frame");

    VkPresentInfoKHR present {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &rendered.semaphore;
    present.swapchainCount = 1;
    present.pSwapchains = &m_swapchain;
    present.pImageIndices = &m_imageIndex;
    const VkResult result = vkQueuePresentKHR(m_queue, &present);
    m_frameStats.presentMilliseconds =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - presentStart).count();
    m_frameStats.draws = m_draws;
    m_frameStats.triangles = m_triangles;
    m_frame = nullptr;
    m_frameIndex = (m_frameIndex + 1) % m_frames.size();
    if (result == VK_SUBOPTIMAL_KHR && CompositorRotates)
        return true;
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        m_recreate = true;
        return result == VK_SUBOPTIMAL_KHR;
    }
    check(result, "Present Vulkan frame");
    return true;
}

BufferPtr VulkanContext::makeMappedBuffer(VkDeviceSize size, VkBufferUsageFlags usage)
{
    auto buffer = std::make_shared<VulkanBuffer>();
    buffer->allocator = m_allocator;
    VkBufferCreateInfo info {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage;
    VmaAllocationCreateInfo allocation = {};
    allocation.usage = VMA_MEMORY_USAGE_AUTO;
    allocation.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo result;
    check(vmaCreateBuffer(m_allocator, &info, &allocation, &buffer->handle, &buffer->allocation, &result),
          "Allocate Vulkan buffer");
    buffer->mapped = result.pMappedData;
    return buffer;
}

BufferPtr VulkanContext::createBuffer(const void *data, size_t size, VkBufferUsageFlags usage)
{
    if (!size)
        return {};
    auto buffer = makeMappedBuffer(size, usage);
    std::memcpy(buffer->mapped, data, size);
    check(vmaFlushAllocation(m_allocator, buffer->allocation, 0, size), "Flush Vulkan buffer");
    return buffer;
}

ImagePtr VulkanContext::makeImage(VkExtent2D size, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect,
                                  VkSampleCountFlagBits samples)
{
    auto image = std::make_shared<VulkanImage>();
    image->device = m_device;
    image->allocator = m_allocator;
    image->size = size;
    image->format = format;
    image->aspect = aspect;
    image->samples = samples;
    VkImageCreateInfo info {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {size.width, size.height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = samples;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    VmaAllocationCreateInfo allocation = {};
    allocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    check(vmaCreateImage(m_allocator, &info, &allocation, &image->handle, &image->allocation, nullptr),
          "Allocate Vulkan image");
    image->view = createView(m_device, image->handle, format, aspect);
    return image;
}

ImagePtr VulkanContext::createTexture(int width, int height, const void *rgbaPixels)
{
    if (width <= 0 || height <= 0 || !rgbaPixels)
        return {};
    auto image = makeImage({uint32_t(width), uint32_t(height)}, ColorFormat,
                           VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
                           VK_SAMPLE_COUNT_1_BIT);
    const size_t bytes = size_t(width) * size_t(height) * 4;
    image->pendingUpload = createBuffer(rgbaPixels, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    return image;
}

ImagePtr VulkanContext::createSurface(int width, int height, bool depth)
{
    if (width <= 0 || height <= 0)
        return {};
    return makeImage({uint32_t(width), uint32_t(height)}, depth ? DepthFormat : ColorFormat,
                     depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                           : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                     depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT);
}

ImagePtr VulkanContext::createSampledDepth(VkExtent2D size)
{
#ifdef __ANDROID__
    constexpr VkFormat format = VK_FORMAT_D16_UNORM;
#else
    constexpr VkFormat format = DepthFormat;
#endif
    return makeImage(size, format, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                     VK_IMAGE_ASPECT_DEPTH_BIT, VK_SAMPLE_COUNT_1_BIT);
}

void VulkanContext::createPostTargets(VkFormat format, bool needsSceneTarget)
{
    if (!m_temporalAA && !m_spatialAA && !needsSceneTarget)
        return;
    constexpr auto Usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    const auto surface = [&](VkFormat format) {
        return makeImage(m_extent, format, Usage, VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT);
    };
    m_post.scene = makeImage(m_extent, format, Usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
                             VK_SAMPLE_COUNT_1_BIT);
    const VkImageUsageFlags depthUsage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                         (m_temporalAA ? VK_IMAGE_USAGE_SAMPLED_BIT : 0);
    if (m_temporalAA || m_spatialAA || m_frames[0]->msaaColor)
        for (auto &depth : m_post.depth)
            depth = makeImage(m_extent, DepthFormat, depthUsage, VK_IMAGE_ASPECT_DEPTH_BIT, VK_SAMPLE_COUNT_1_BIT);
    if (m_temporalAA)
        for (auto &history : m_post.history)
            history = surface(ColorFormat);
    if (m_spatialAA) {
        m_post.edges = surface(VK_FORMAT_R8G8_UNORM);
        m_post.weights = surface(ColorFormat);
        const auto texture = [&](int width, int height, VkFormat format, int channels, const unsigned char *data) {
            auto image = makeImage({uint32_t(width), uint32_t(height)}, format,
                                   VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                   VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT);
            image->pendingUpload =
                createBuffer(data, size_t(width) * height * channels, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
            return image;
        };
        m_post.area = texture(AREATEX_WIDTH, AREATEX_HEIGHT, VK_FORMAT_R8G8_UNORM, 2, areaTexBytes);
        m_post.search = texture(SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, VK_FORMAT_R8_UNORM, 1, searchTexBytes);
    }
}

void VulkanContext::finishScene(const TemporalFrame &temporal)
{
    if (!m_frame || !m_post.scene || m_post.resolved)
        return;
    m_post.resolved = true;
    const auto oldTextures = m_textures;
    const auto oldState = m_state;
    const auto *oldUniforms = m_uniforms;
    const auto oldUniformSize = m_uniformSize;
    const float width = float(m_extent.width), height = float(m_extent.height);
    struct {
        std::array<float, 16> clip;
        std::array<float, 4> metrics;
        std::array<float, 4> jitter;
        std::array<float, 4> tuning;
    } uniforms {temporal.clipToPrevious, {1 / width, 1 / height, width, height}, temporal.jitterPixels, {}};

    const auto pass = [&](VulkanProgram program, const ImagePtr &color, const ImagePtr &depth,
                          const std::array<ImagePtr, 4> &textures) {
        endRendering();
        m_targets.back() = {color, depth, {}};
        m_textures = textures;
        beginRendering(true);
        setDrawState({program, RasterizerTwoSided, DepthDisabled, BlendOpaque}, &uniforms, sizeof(uniforms));
        bindDrawState(true);
        vkCmdDraw(m_frame->command, 3, 1, 0, 0);
    };

    ImagePtr resolved = m_post.scene;
    if (m_temporalAA && temporal.reproject) {
        const int current = int(m_frameIndex);
        const int previous = m_post.lastHistory;
        const bool useHistory = previous >= 0 && previous != current;
        uniforms.tuning = {0.72f, 0.86f, 0.08f, useHistory ? 1.0f : 0.0f};
        const auto &history = m_post.history[current];
        pass(VulkanProgram::TemporalResolve, history, {},
             {m_post.scene, useHistory ? m_post.history[previous] : ImagePtr {}, m_post.depth[current],
              useHistory ? m_post.depth[previous] : ImagePtr {}});
        resolved = history;
        m_post.lastHistory = current;
    } else {
        m_post.lastHistory = -1;
    }

    const auto &backBuffer = m_swapImages[m_imageIndex];
    const auto &overlayDepth = m_frame->msaaColor ? m_post.depth[m_frameIndex] : m_frame->depth;
    if (m_spatialAA) {
        pass(VulkanProgram::SmaaEdges, m_post.edges, {}, {resolved, {}, {}, {}});
        pass(VulkanProgram::SmaaWeights, m_post.weights, {}, {m_post.edges, m_post.area, m_post.search, {}});
        pass(VulkanProgram::SmaaBlend, backBuffer, overlayDepth, {resolved, m_post.weights, {}, {}});
    } else {
        pass(VulkanProgram::PostCopy, backBuffer, overlayDepth, {resolved, {}, {}, {}});
    }
    m_textures = oldTextures;
    setDrawState(oldState, oldUniforms, oldUniformSize);
}

void VulkanContext::transition(const ImagePtr &image, VkImageLayout layout, VkPipelineStageFlags2 stage,
                               VkAccessFlags2 access)
{
    VkImageMemoryBarrier2 barrier {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = image->stage;
    barrier.srcAccessMask = image->access;
    barrier.dstStageMask = stage;
    barrier.dstAccessMask = access;
    barrier.oldLayout = image->layout;
    barrier.newLayout = layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image->handle;
    barrier.subresourceRange = {image->aspect, 0, 1, 0, 1};
    VkDependencyInfo dependency {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(m_frame->command, &dependency);
    image->layout = layout;
    image->stage = stage;
    image->access = access;
}

void VulkanContext::uploadTexture(const ImagePtr &image)
{
    transition(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
               VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkBufferImageCopy copy = {};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {image->size.width, image->size.height, 1};
    vkCmdCopyBufferToImage(m_frame->command, image->pendingUpload->handle, image->handle,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    m_frame->inUse.push_back(std::move(image->pendingUpload));
}

void VulkanContext::beginRendering(bool clear, std::array<float, 4> clearColor, const ImagePtr &resolveDepth)
{
    const Target &target = m_targets.back();
    auto prepareColor = [this](const ImagePtr &image) {
        transition(image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    };
    VkRenderingAttachmentInfo color {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    if (target.color) {
        prepareColor(target.color);
        color.imageView = target.color->view;
    }
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    std::copy(clearColor.begin(), clearColor.end(), color.clearValue.color.float32);
    if (target.resolve) {
        prepareColor(target.resolve);
        color.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        color.resolveImageView = target.resolve->view;
        color.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    VkRenderingAttachmentInfo depth {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    if (target.depth) {
        VkPipelineStageFlags2 stages =
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        VkAccessFlags2 access =
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        if (resolveDepth) {
            stages |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            access |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
        }
        transition(target.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, stages, access);
        depth.imageView = target.depth->view;
        depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth.loadOp = color.loadOp;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth.clearValue.depthStencil = {1.0f, 0};
        if (resolveDepth) {
            transition(resolveDepth, depth.imageLayout, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
            depth.resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
            depth.resolveImageView = resolveDepth->view;
            depth.resolveImageLayout = depth.imageLayout;
        }
    }

    const VkExtent2D size = target.color ? target.color->size : target.depth->size;
    VkRenderingInfo info {VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea.extent = size;
    info.layerCount = 1;
    info.colorAttachmentCount = target.color ? 1 : 0;
    info.pColorAttachments = target.color ? &color : nullptr;
    info.pDepthAttachment = target.depth ? &depth : nullptr;
    vkCmdBeginRendering(m_frame->command, &info);
    // The existing projections use an upward clip-space Y and a downward screen-space Y.
    const VkViewport viewport {0.0f, float(size.height), float(size.width), -float(size.height), 0.0f, 1.0f};
    const VkRect2D scissor {{0, 0}, size};
    vkCmdSetViewport(m_frame->command, 0, 1, &viewport);
    vkCmdSetScissor(m_frame->command, 0, 1, &scissor);
    m_rendering = true;
}

void VulkanContext::endRendering()
{
    if (m_rendering) {
        vkCmdEndRendering(m_frame->command);
        m_rendering = false;
    }
}

void VulkanContext::beginSurface(const ImagePtr &color, const ImagePtr &depth)
{
    if (!m_frame)
        return;
    endRendering();
    m_targets.push_back({color, depth, {}});
    if (color)
        m_frame->inUse.push_back(color);
    if (depth)
        m_frame->inUse.push_back(depth);
    beginRendering(true);
}

void VulkanContext::endSurface()
{
    if (!m_frame)
        return;
    endRendering();
    m_targets.pop_back();
    beginRendering(false);
}

void VulkanContext::snapshotWaterBackground()
{
    if (!m_frame)
        return;
    const Target target = m_targets.back();
    const auto &color = target.resolve ? target.resolve : target.color;
    constexpr auto SnapshotUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (!m_waterColor) {
        m_waterColor =
            makeImage(color->size, color->format, SnapshotUsage, VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT);
        m_waterDepth =
            makeImage(target.depth->size, DepthFormat, SnapshotUsage | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                      VK_IMAGE_ASPECT_DEPTH_BIT, VK_SAMPLE_COUNT_1_BIT);
    }

    endRendering();
    const auto copy = [&](const ImagePtr &source, const ImagePtr &snapshot) {
        transition(source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                   VK_ACCESS_2_TRANSFER_READ_BIT);
        transition(snapshot, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                   VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkImageCopy region = {};
        region.srcSubresource = region.dstSubresource = {source->aspect, 0, 0, 1};
        region.extent = {source->size.width, source->size.height, 1};
        vkCmdCopyImage(m_frame->command, source->handle, source->layout, snapshot->handle, snapshot->layout, 1,
                       &region);
    };
    copy(color, m_waterColor);
    if (target.depth->samples == VK_SAMPLE_COUNT_1_BIT) {
        copy(target.depth, m_waterDepth);
    } else {
        // Resolve depth once, before the water depth pass replaces the background depth.
        m_targets.push_back({{}, target.depth, {}});
        beginRendering(false, {}, m_waterDepth);
        endRendering();
        m_targets.pop_back();
    }
    for (const auto &snapshot : {m_waterColor, m_waterDepth})
        transition(snapshot, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                   VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    beginRendering(false);
}

void VulkanContext::setDrawState(VulkanDrawState state, const void *uniforms, size_t uniformSize)
{
    m_pipelineSource = nullptr;
    m_scissorEnabled = false;
    m_state = state;
    m_uniforms = uniforms;
    m_uniformSize = uniformSize;
}

void VulkanContext::setDrawState(VulkanPipelineSource &source, const void *uniforms, size_t uniformSize,
                                 const VkRect2D *scissor)
{
    m_pipelineSource = &source;
    m_uniforms = uniforms;
    m_uniformSize = uniformSize;
    m_scissorEnabled = scissor != nullptr;
    if (scissor)
        m_drawScissor = *scissor;
}

static VkRect2D clipRectangle(VkRect2D rect, VkExtent2D size)
{
    const int32_t left = std::clamp(rect.offset.x, 0, int32_t(size.width));
    const int32_t top = std::clamp(rect.offset.y, 0, int32_t(size.height));
    const auto right = std::clamp<int64_t>(int64_t(rect.offset.x) + rect.extent.width, left, size.width);
    const auto bottom = std::clamp<int64_t>(int64_t(rect.offset.y) + rect.extent.height, top, size.height);
    return {{left, top}, {uint32_t(right - left), uint32_t(bottom - top)}};
}

void VulkanContext::clearAttachments(const VkClearColorValue *color, const float *depth, const VkRect2D *rectangle)
{
    if (!m_rendering)
        return;
    const Target &target = m_targets.back();
    const VkExtent2D size = target.color ? target.color->size : target.depth->size;
    VkClearRect rect {rectangle ? clipRectangle(*rectangle, size) : VkRect2D {{0, 0}, size}, 0, 1};
    VkClearAttachment attachments[2] = {};
    uint32_t count = 0;
    if (color && target.color) {
        attachments[count].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        attachments[count++].clearValue.color = *color;
    }
    if (depth && target.depth) {
        attachments[count].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        attachments[count++].clearValue.depthStencil.depth = *depth;
    }
    if (count && rect.rect.extent.width && rect.rect.extent.height)
        vkCmdClearAttachments(m_frame->command, count, attachments, 1, &rect);
}

void VulkanContext::bindTexture(unsigned slot, const ImagePtr &image)
{
    m_textures.at(slot) = image;
}

void VulkanContext::bindVertex(const BufferPtr &buffer)
{
    m_vertex = {buffer, 0};
}

void VulkanContext::bindIndex(const BufferPtr &buffer, VkIndexType type)
{
    m_index = {buffer, 0};
    m_indexType = type;
}

VulkanContext::BufferSlice VulkanContext::stream(const void *data, VkDeviceSize size, VkDeviceSize alignment)
{
    const VkDeviceSize required = std::max(size, UniformCapacity);
    for (;;) {
        if (m_frame->pageIndex == m_frame->pages.size()) {
            const VkDeviceSize capacity = std::max(required, StreamPageSize);
            auto buffer =
                makeMappedBuffer(capacity, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                               VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
            m_frame->pages.push_back({std::move(buffer), capacity});
        }
        auto &page = m_frame->pages[m_frame->pageIndex];
        const VkDeviceSize offset = (page.used + alignment - 1) & ~(alignment - 1);
        if (offset + required <= page.capacity) {
            std::memcpy(static_cast<char *>(page.buffer->mapped) + offset, data, static_cast<size_t>(size));
            page.used = offset + size;
            return {page.buffer, offset};
        }
        ++m_frame->pageIndex;
    }
}

VkDescriptorSet VulkanContext::allocateSet(VkDescriptorSetLayout layout)
{
    for (;;) {
        if (m_frame->poolIndex == m_frame->pools.size()) {
            const VkDescriptorPoolSize sizes[] = {
                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, BufferSetsPerPool},
                {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, ImageSetsPerPool * static_cast<uint32_t>(ImageBindings.size())},
                {VK_DESCRIPTOR_TYPE_SAMPLER, ImageSetsPerPool * 4},
            };
            VkDescriptorPoolCreateInfo info {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            info.maxSets = BufferSetsPerPool + ImageSetsPerPool;
            info.poolSizeCount = 3;
            info.pPoolSizes = sizes;
            VkDescriptorPool pool;
            check(vkCreateDescriptorPool(m_device, &info, nullptr, &pool), "Create Vulkan descriptor pool");
            m_frame->pools.push_back(pool);
        }
        VkDescriptorSetAllocateInfo info {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        info.descriptorPool = m_frame->pools[m_frame->poolIndex];
        info.descriptorSetCount = 1;
        info.pSetLayouts = &layout;
        VkDescriptorSet set;
        const VkResult result = vkAllocateDescriptorSets(m_device, &info, &set);
        if (result == VK_SUCCESS)
            return set;
        if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL)
            check(result, "Allocate Vulkan descriptors");
        ++m_frame->poolIndex;
    }
}

VkDescriptorSet VulkanContext::textureSet()
{
    const Target &target = m_targets.back();
    std::array<ImagePtr, ImageBindings.size()> textures;
    if (m_pipelineSource)
        textures = m_textures;
    else switch (m_state.program) {
    case VulkanProgram::Textured:
        textures[0] = m_textures[0];
        break;
    case VulkanProgram::Interface:
        textures[0] = m_textures[0];
        textures[1] = m_textures[1];
        break;
    case VulkanProgram::World:
        textures[1] = m_textures[1];
        textures[2] = m_textures[2];
        textures[3] = m_textures[3];
        break;
    case VulkanProgram::Water:
        textures[0] = m_waterColor;
        textures[2] = m_textures[2];
        textures[3] = m_waterDepth;
        break;
    default:
        textures = m_textures;
        break;
    }
    for (auto &image : textures) {
        if (!image || image == target.color || image == target.depth || image == target.resolve ||
            (!image->pendingUpload && image->layout == VK_IMAGE_LAYOUT_UNDEFINED))
            image = m_emptyTexture;
        if (image->layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            continue;
        endRendering();
        if (image->pendingUpload)
            uploadTexture(image);
        transition(image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                   VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    }
    if (!m_rendering)
        beginRendering(false);

    std::array<VkImageView, ImageBindings.size()> key;
    for (size_t i = 0; i < key.size(); ++i)
        key[i] = textures[i]->view;
    auto samplers = m_textureSamplers;
    for (auto &sampler : samplers)
        if (!sampler) sampler = m_sampler;
    const auto descriptorKey = std::make_pair(key, samplers);
    const auto cached = m_frame->imageSets.find(descriptorKey);
    if (cached != m_frame->imageSets.end())
        return cached->second;

    const VkDescriptorSet set = allocateSet(m_textureLayout);
    VkDescriptorImageInfo images[ImageBindings.size() + 2] = {};
    VkWriteDescriptorSet writes[ImageBindings.size() + 2] = {};
    for (uint32_t slot = 0; slot < ImageBindings.size(); ++slot) {
        images[slot].imageView = textures[slot]->view;
        images[slot].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        writes[slot].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[slot].dstSet = set;
        writes[slot].dstBinding = ImageBindings[slot];
        writes[slot].descriptorCount = 1;
        writes[slot].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writes[slot].pImageInfo = &images[slot];
        m_frame->inUse.push_back(textures[slot]);
    }
    for (uint32_t i = 0; i < samplers.size(); ++i) {
        const size_t index = ImageBindings.size() + i;
        images[index].sampler = samplers[i];
        writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[index].dstSet = set;
        writes[index].dstBinding = 6 + i;
        writes[index].descriptorCount = 1;
        writes[index].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        writes[index].pImageInfo = &images[index];
    }
    vkUpdateDescriptorSets(m_device, uint32_t(std::size(writes)), writes, 0, nullptr);
    m_frame->imageSets.emplace(descriptorKey, set);
    return set;
}

void VulkanContext::drawTransient(const void *vertices, size_t vertexBytes, const uint32_t *indices,
                                  uint32_t indexCount)
{
    if (!m_frame || !vertexBytes || !indexCount)
        return;
    m_vertex = stream(vertices, vertexBytes, alignof(float));
    m_index = stream(indices, sizeof(uint32_t) * size_t(indexCount), alignof(uint32_t));
    m_indexType = VK_INDEX_TYPE_UINT32;
    drawIndexed(indexCount);
}

void VulkanContext::drawIndexed(uint32_t indexCount, uint32_t firstIndex, int32_t vertexOffset)
{
    if (!m_frame || !indexCount || !m_vertex.buffer || !m_index.buffer || !m_uniforms)
        return;
    const bool textured = m_pipelineSource || m_state.program == VulkanProgram::Textured || m_state.program == VulkanProgram::Interface ||
                          m_state.program == VulkanProgram::World || m_state.program == VulkanProgram::Water;
    bindDrawState(textured);
    const auto command = m_frame->command;
    vkCmdBindVertexBuffers(command, 0, 1, &m_vertex.buffer->handle, &m_vertex.offset);
    vkCmdBindIndexBuffer(command, m_index.buffer->handle, m_index.offset, m_indexType);
    vkCmdDrawIndexed(command, indexCount, 1, firstIndex, vertexOffset, 0);
    ++m_draws;
    m_triangles += indexCount / 3;
    // Meshes and drawings can be rebuilt before the GPU completes this frame.
    m_frame->inUse.push_back(m_vertex.buffer);
    m_frame->inUse.push_back(m_index.buffer);
}

bool VulkanContext::drawInstanced(uint32_t indexCount, const void *instances, uint32_t instanceCount)
{
    if (m_pipelineSource || (m_state.program != VulkanProgram::World && m_state.program != VulkanProgram::Depth &&
                             m_state.program != VulkanProgram::Shadow))
        return false;
    if (!m_frame || !indexCount || !instanceCount || !m_vertex.buffer || !m_index.buffer || !m_uniforms)
        return true;
    const BufferSlice records = stream(instances, VkDeviceSize(instanceCount) * InstanceRecordSize, alignof(float));
    m_instanced = true;
    bindDrawState(m_state.program == VulkanProgram::World);
    m_instanced = false;
    const auto command = m_frame->command;
    const VkBuffer buffers[] = {m_vertex.buffer->handle, records.buffer->handle};
    const VkDeviceSize offsets[] = {m_vertex.offset, records.offset};
    vkCmdBindVertexBuffers(command, 0, 2, buffers, offsets);
    vkCmdBindIndexBuffer(command, m_index.buffer->handle, m_index.offset, m_indexType);
    vkCmdDrawIndexed(command, indexCount, instanceCount, 0, 0, 0);
    ++m_draws;
    m_triangles += uint64_t(indexCount / 3) * instanceCount;
    m_frame->inUse.push_back(m_vertex.buffer);
    m_frame->inUse.push_back(m_index.buffer);
    return true;
}

void VulkanContext::bindDrawState(bool textured)
{
    if (m_uniformSize > UniformCapacity)
        throw std::logic_error("Vulkan draw constants exceed their descriptor range.");
    const VkDescriptorSet textures = textured ? textureSet() : VK_NULL_HANDLE;
    const auto constants = stream(m_uniforms, m_uniformSize, m_properties.limits.minUniformBufferOffsetAlignment);
    auto &page = m_frame->pages[m_frame->pageIndex];
    if (!page.uniforms) {
        page.uniforms = allocateSet(m_uniformLayout);
        VkDescriptorBufferInfo buffer {constants.buffer->handle, 0, UniformCapacity};
        VkWriteDescriptorSet write {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = page.uniforms;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        write.pBufferInfo = &buffer;
        vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
    }

    const VkCommandBuffer command = m_frame->command;
    if (m_pipelineSource) {
        const Target &target = m_targets.back();
        const VkExtent2D size = target.color ? target.color->size : target.depth->size;
        const VkRect2D rect = m_scissorEnabled ? clipRectangle(m_drawScissor, size) : VkRect2D {{0, 0}, size};
        vkCmdSetScissor(command, 0, 1, &rect);
    }
    const VkPipeline selected = pipeline();
    if (selected != m_frame->boundPipeline) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, selected);
        m_frame->boundPipeline = selected;
    }
    const VkDescriptorSet sets[] = {page.uniforms, textures};
    const uint32_t dynamicOffset = static_cast<uint32_t>(constants.offset);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, textured ? 2 : 1, sets, 1,
                            &dynamicOffset);
}

VkPipeline VulkanContext::pipeline()
{
    const Target &target = m_targets.back();
    const VkFormat colorFormat = target.color ? target.color->format : VK_FORMAT_UNDEFINED;
    const VkFormat depthFormat = target.depth ? target.depth->format : VK_FORMAT_UNDEFINED;
    const auto samples = target.color ? target.color->samples : target.depth->samples;
    if (m_pipelineSource)
        return m_pipelineSource->pipeline(m_device, m_pipelineLayout, colorFormat, depthFormat, samples);
    const auto key = std::make_tuple(m_state.program, m_state.rasterizer, m_state.depth, m_state.blend, colorFormat,
                                     depthFormat, samples, m_instanced);
    const auto cached = m_pipelines.find(key);
    if (cached != m_pipelines.end())
        return cached->second;

    VkVertexInputBindingDescription bindings[] = {
        {0, 8, VK_VERTEX_INPUT_RATE_VERTEX},
        {1, InstanceRecordSize, VK_VERTEX_INPUT_RATE_INSTANCE},
    };
    VkVertexInputBindingDescription &binding = bindings[0];
    // Room for the instance record's matrix rows and material colour after the mesh's own attributes.
    VkVertexInputAttributeDescription attributes[8] = {
        {0, 0, VK_FORMAT_R8G8B8A8_UNORM, 0},
        {1, 0, VK_FORMAT_R8G8B8A8_UNORM, 4},
        {2, 0, VK_FORMAT_R32G32_SFLOAT, 20},
    };
    uint32_t attributesUsed = 2;
    size_t vertexShader = m_instanced ? 7 : 0;
    if (m_state.program == VulkanProgram::Textured) {
        vertexShader = 1;
        binding.stride = 28;
        attributes[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributes[1].offset = 16;
        attributesUsed = 3;
    } else if (m_state.program == VulkanProgram::Interface) {
        vertexShader = 2;
        binding.stride = 32;
        attributes[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributes[1].offset = 16;
    } else if (m_state.program == VulkanProgram::Depth) {
        vertexShader = m_instanced ? 10 : 9;
        attributesUsed = 1;
    } else if (m_state.program == VulkanProgram::Shadow) {
        vertexShader = m_instanced ? 8 : 3;
        attributesUsed = 1;
    } else if (m_state.program == VulkanProgram::ReflectiveShadow) {
        vertexShader = 5;
    } else if (m_state.program == VulkanProgram::Water) {
        vertexShader = 6;
        attributesUsed = 1;
    } else if (m_state.program >= VulkanProgram::PostCopy) {
        vertexShader = 4;
        attributesUsed = 0;
    }
    uint32_t bindingsUsed = attributesUsed ? 1 : 0;
    if (m_instanced) {
        // Locations 3 to 6 are the world matrix's rows; the World program also reads the material colour at 7 and the
        // alpha at 8.
        const uint32_t rows = m_state.program == VulkanProgram::World ? 5 : 4;
        for (uint32_t row = 0; row < rows; ++row)
            attributes[attributesUsed++] = {3 + row, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 16 * row};
        if (m_state.program == VulkanProgram::World)
            attributes[attributesUsed++] = {8, 1, VK_FORMAT_R32_SFLOAT, 80};
        bindingsUsed = 2;
    }
    VkPipelineVertexInputStateCreateInfo vertex {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex.vertexBindingDescriptionCount = bindingsUsed;
    vertex.pVertexBindingDescriptions = bindings;
    vertex.vertexAttributeDescriptionCount = attributesUsed;
    vertex.pVertexAttributeDescriptions = attributes;
    VkPipelineInputAssemblyStateCreateInfo assembly {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = m_state.rasterizer == RasterizerTwoSided ? VK_CULL_MODE_NONE : VK_CULL_MODE_FRONT_BIT;
    raster.frontFace = m_state.rasterizer == RasterizerBack ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = samples;
    VkPipelineDepthStencilStateCreateInfo depth {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = target.depth && m_state.depth != DepthDisabled;
    depth.depthWriteEnable = depth.depthTestEnable && m_state.depth == DepthTestWrite;
    depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState attachment = {};
    attachment.blendEnable = m_state.blend == BlendAlpha;
    attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    attachment.colorBlendOp = VK_BLEND_OP_ADD;
    attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    attachment.dstAlphaBlendFactor =
        m_state.program == VulkanProgram::Interface ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA : VK_BLEND_FACTOR_ZERO;
    attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    if (m_state.blend != BlendDepthOnly)
        attachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = target.color ? 1 : 0;
    blend.pAttachments = target.color ? &attachment : nullptr;

    const VkDynamicState dynamics[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamics;
    const VkPipelineShaderStageCreateInfo stages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,
         m_vertexShaders[vertexShader], "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT,
         m_fragmentShaders[static_cast<size_t>(m_state.program)], "main", nullptr},
    };
    VkPipelineRenderingCreateInfo rendering {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = target.color ? 1 : 0;
    rendering.pColorAttachmentFormats = target.color ? &colorFormat : nullptr;
    rendering.depthAttachmentFormat = depthFormat;
    VkGraphicsPipelineCreateInfo info {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.pNext = &rendering;
    info.stageCount = stages[1].module ? 2 : 1;
    info.pStages = stages;
    info.pVertexInputState = &vertex;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = m_pipelineLayout;
    VkPipeline created;
    check(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &info, nullptr, &created), "Create Vulkan pipeline");
    m_pipelines.emplace(key, created);
    return created;
}

}

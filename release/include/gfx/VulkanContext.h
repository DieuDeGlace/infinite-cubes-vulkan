#pragma once

#include <array>
#include <functional>
#include <iosfwd>
#include <map>
#include <memory>
#include <tuple>
#include <vector>
#include <volk.h>
#include "gfx/RendererIds.h"

struct VmaAllocator_T;

namespace gfx {

struct VulkanBuffer;
struct VulkanImage;
using BufferPtr = std::shared_ptr<VulkanBuffer>;
using ImagePtr = std::shared_ptr<VulkanImage>;

enum class VulkanProgram {
    World,
    Sky,
    Water,
    Clouds,
    Textured,
    Interface,
    Shadow,
    ReflectiveShadow,
    PostCopy,
    TemporalResolve,
    SmaaEdges,
    SmaaWeights,
    SmaaBlend,
    Count
};

struct VulkanDrawState {
    VulkanProgram program;
    RasterizerSlot rasterizer;
    DepthStencilSlot depth;
    BlendSlot blend;
};

struct TemporalFrame {
    std::array<float, 16> clipToPrevious = {};
    std::array<float, 4> jitterPixels = {};
    bool reproject = false;
};

// Owns the platform surface, device, and command recording for the game and Plasma.
class VulkanContext {
public:
    VulkanContext();
    ~VulkanContext();
    VulkanContext(const VulkanContext &) = delete;
    VulkanContext &operator=(const VulkanContext &) = delete;

    using SurfaceFactory = std::function<VkResult(VkInstance, VkSurfaceKHR *)>;
    void initialize(const char *surfaceExtension, const SurfaceFactory &createSurface, std::ostream &startupLog);
    // Android destroys the window whenever the app leaves the screen. The device and everything created on it stay;
    // beginFrame returns an empty extent until replaceSurface supplies the next window.
    void releaseSurface();
    void replaceSurface(const SurfaceFactory &createSurface);
    VkExtent2D beginFrame(VkExtent2D clientSize, bool vsync, unsigned requestedSamples, bool temporalAA, bool spatialAA,
                          std::array<float, 4> clearColor = {1, 1, 1, 1});
    bool temporalEnabled() const { return m_temporalAA; }
    void finishScene(const TemporalFrame &temporal = {});
    bool present();
    void waitIdle() const;

    BufferPtr createBuffer(const void *data, size_t size, VkBufferUsageFlags usage);
    ImagePtr createTexture(int width, int height, const void *rgbaPixels);
    ImagePtr createSurface(int width, int height, bool depth);
    ImagePtr createSampledDepth(VkExtent2D size);
    void beginSurface(const ImagePtr &color, const ImagePtr &depth);
    void endSurface();
    void snapshotWaterBackground();

    void setDrawState(VulkanDrawState state, const void *uniforms, size_t uniformSize);
    void bindTexture(unsigned slot, const ImagePtr &image);
    void bindVertex(const BufferPtr &buffer);
    void bindIndex(const BufferPtr &buffer, VkIndexType type);
    void drawIndexed(uint32_t indexCount);
    void drawTransient(const void *vertices, size_t vertexBytes, const uint32_t *indices, uint32_t indexCount);

private:
    struct BufferSlice {
        BufferPtr buffer;
        VkDeviceSize offset = 0;
    };
    struct Target {
        ImagePtr color;
        ImagePtr depth;
        ImagePtr resolve;
    };
    struct Frame;
    struct StreamPage;
    struct PostTargets {
        ImagePtr scene;
        std::array<ImagePtr, 2> depth;
        std::array<ImagePtr, 2> history;
        ImagePtr edges, weights, area, search;
        int lastHistory = -1;
        bool resolved = false;
    };

    void createDevice();
    void createLayouts();
    void createShaders();
    bool recreateSwapchain(VkExtent2D clientSize, bool vsync, unsigned requestedSamples);
    void destroySwapchain();
    void beginRendering(bool clear, std::array<float, 4> color = {0, 0, 0, 0}, const ImagePtr &resolveDepth = {});
    void endRendering();
    void transition(const ImagePtr &image, VkImageLayout layout, VkPipelineStageFlags2 stage, VkAccessFlags2 access);
    void uploadTexture(const ImagePtr &image);
    ImagePtr makeImage(VkExtent2D size, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect,
                       VkSampleCountFlagBits samples);
    BufferPtr makeMappedBuffer(VkDeviceSize size, VkBufferUsageFlags usage);
    BufferSlice stream(const void *data, VkDeviceSize size, VkDeviceSize alignment);
    VkDescriptorSet allocateSet(VkDescriptorSetLayout layout);
    VkDescriptorSet textureSet();
    VkPipeline pipeline();
    void bindDrawState(bool textured);
    void createPostTargets(VkFormat format, bool needsSceneTarget);

    VkInstance m_instance = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties m_properties = {};
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    uint32_t m_queueFamily = 0;
    VmaAllocator_T *m_allocator = nullptr;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_uniformLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_textureLayout = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    VkSampler m_pointSampler = VK_NULL_HANDLE;
    std::array<VkShaderModule, 7> m_vertexShaders = {};
    std::array<VkShaderModule, static_cast<size_t>(VulkanProgram::Count)> m_fragmentShaders = {};
    using PipelineKey = std::tuple<VulkanProgram, RasterizerSlot, DepthStencilSlot, BlendSlot, VkFormat, VkFormat,
                                   VkSampleCountFlagBits>;
    std::map<PipelineKey, VkPipeline> m_pipelines;

    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkExtent2D m_extent = {};
    VkExtent2D m_clientSize = {};
    VkSampleCountFlagBits m_samples = VK_SAMPLE_COUNT_1_BIT;
    unsigned m_requestedSamples = 0;
    bool m_vsync = true;
    bool m_temporalAA = false;
    bool m_spatialAA = false;
    bool m_recreate = false;
    PostTargets m_post;
    ImagePtr m_waterColor, m_waterDepth;
    std::vector<ImagePtr> m_swapImages;
    std::vector<VkSemaphore> m_renderFinished;
    uint32_t m_imageIndex = 0;
    std::array<std::unique_ptr<Frame>, 2> m_frames;
    size_t m_frameIndex = 0;
    Frame *m_frame = nullptr;
    std::vector<Target> m_targets;
    bool m_rendering = false;

    ImagePtr m_emptyTexture;
    std::array<ImagePtr, 4> m_textures;
    BufferSlice m_vertex;
    BufferSlice m_index;
    VkIndexType m_indexType = VK_INDEX_TYPE_UINT32;
    VulkanDrawState m_state = {};
    const void *m_uniforms = nullptr;
    size_t m_uniformSize = 0;
};

}

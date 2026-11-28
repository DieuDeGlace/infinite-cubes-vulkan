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
    Depth,
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

// What the last presented frame cost: the CPU's wait for an earlier frame's GPU work and for a swapchain image, the
// time submitting and presenting took, the GPU's own time for the frame (0 where the queue has no timestamps), and
// what it drew.
struct VulkanFrameStats {
    float waitMilliseconds = 0;
    float presentMilliseconds = 0;
    float gpuMilliseconds = 0;
    uint32_t draws = 0;
    uint64_t triangles = 0;
};

struct TemporalFrame {
    std::array<float, 16> clipToPrevious = {};
    std::array<float, 4> jitterPixels = {};
    bool reproject = false;
};

// Clients with their own vertex formats and shaders cache pipelines for the current attachments.
class VulkanPipelineSource {
public:
    virtual VkPipeline pipeline(VkDevice device, VkPipelineLayout layout, VkFormat color, VkFormat depth,
                                VkSampleCountFlagBits samples) = 0;

protected:
    ~VulkanPipelineSource() = default;
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
    const VulkanFrameStats &frameStats() const { return m_frameStats; }
    void waitIdle() const;

    BufferPtr createBuffer(const void *data, size_t size, VkBufferUsageFlags usage);
    ImagePtr createTexture(int width, int height, const void *rgbaPixels);
    ImagePtr createSurface(int width, int height, bool depth);
    ImagePtr createSampledDepth(VkExtent2D size);
    void beginSurface(const ImagePtr &color, const ImagePtr &depth);
    void endSurface();
    void snapshotWaterBackground();
    void bindWaterBackground()
    {
        bindTexture(0, m_waterColor);
        bindTexture(3, m_waterDepth);
    }
    void setTextureSampler(unsigned slot, VkFilter min, VkFilter mag, VkSamplerAddressMode u, VkSamplerAddressMode v);

    void setDrawState(VulkanDrawState state, const void *uniforms, size_t uniformSize);
    void setDrawState(VulkanPipelineSource &source, const void *uniforms, size_t uniformSize, const VkRect2D *scissor);
    void clearAttachments(const VkClearColorValue *color, const float *depth, const VkRect2D *rectangle);
    void bindTexture(unsigned slot, const ImagePtr &image);
    void bindVertex(const BufferPtr &buffer);
    void bindIndex(const BufferPtr &buffer, VkIndexType type);
    void drawIndexed(uint32_t indexCount, uint32_t firstIndex = 0, int32_t vertexOffset = 0);
    // Draws the bound mesh once per 84-byte instance record (VulkanSpriteInstance) with the World or Shadow program;
    // false, with nothing drawn, for any other program.
    bool drawInstanced(uint32_t indexCount, const void *instances, uint32_t instanceCount);
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
    VulkanPipelineSource *m_pipelineSource = nullptr;
    VkRect2D m_drawScissor = {};
    bool m_scissorEnabled = false;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties m_properties = {};
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    uint32_t m_queueFamily = 0;
    uint32_t m_timestampBits = 0; // the queue family's valid timestamp bits; 0 without timestamps
    VmaAllocator_T *m_allocator = nullptr;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_uniformLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_textureLayout = VK_NULL_HANDLE;
    std::map<std::tuple<VkFilter, VkFilter, VkSamplerAddressMode, VkSamplerAddressMode>, VkSampler> m_samplers;
    std::array<VkSampler, 2> m_textureSamplers {};
    VkSampler m_sampler = VK_NULL_HANDLE;
    VkSampler m_pointSampler = VK_NULL_HANDLE;
    std::array<VkShaderModule, 11> m_vertexShaders = {};
    std::array<VkShaderModule, static_cast<size_t>(VulkanProgram::Count)> m_fragmentShaders = {};
    // The last element: the pipeline reads instance records (drawInstanced).
    using PipelineKey = std::tuple<VulkanProgram, RasterizerSlot, DepthStencilSlot, BlendSlot, VkFormat, VkFormat,
                                   VkSampleCountFlagBits, bool>;
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
    bool m_instanced = false; // drawInstanced's draw is being bound
    const void *m_uniforms = nullptr;
    size_t m_uniformSize = 0;
    // The frame being recorded's draws and triangles, published to m_frameStats by present.
    uint32_t m_draws = 0;
    uint64_t m_triangles = 0;
    VulkanFrameStats m_frameStats;
};

}

#pragma once
#include "gfx/AlphaRenderDevice.h"
#include <tuple>
#include "gfx/VulkanWorldData.h"

struct AlphaRenderDevice::DrawData {
    // The vertex and pixel register layouts are shared with Alpha's ported HLSL.
    float constants[64][4] = {};
    AlphaShaderId vs = AlphaShaderId::FixedVertexShader, ps = AlphaShaderId::FixedPixelShader;
    std::array<DWORD, 256> renderStates {};
    struct Sampler {
        VkFilter min = VK_FILTER_NEAREST, mag = VK_FILTER_NEAREST;
        VkSamplerAddressMode u = VK_SAMPLER_ADDRESS_MODE_REPEAT, v = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    };
    std::array<Sampler, 2> samplers;
    std::array<AlphaTexture *, 2> textures {};
    AlphaBuffer *vertices = nullptr, *indices = nullptr;
    unsigned stride = 0, streamStride = 0, offset = 0;
    std::vector<uint32_t> transientIndices;
    VkExtent2D extent {};
    gfx::ImagePtr questIcons;
    gfx::VulkanWorldUniforms markerUniforms {};
    std::vector<gfx::TexturedVertex> markerVertices;
    std::vector<uint32_t> markerIndices;
    VkRect2D scissor {};
    D3DPRIMITIVETYPE primitive = D3DPT_TRIANGLELIST;

    AlphaSurface backBuffer, backDepth;
    AlphaSurface *color = &backBuffer, *depth = &backDepth;
    std::vector<AlphaSurface *> targets {&backBuffer};
    bool targetChanged = false, inFrame = false, firstFrame = true;
    VkDevice device = VK_NULL_HANDLE;
    std::array<VkShaderModule, size_t(AlphaShaderId::Count)> modules {};
    using PipelineKey = std::tuple<AlphaShaderId, AlphaShaderId, unsigned, D3DPRIMITIVETYPE, std::array<DWORD, 12>,
                                   VkFormat, VkFormat, VkSampleCountFlagBits>;
    std::map<PipelineKey, VkPipeline> pipelines;
    ~DrawData();
    void applyTarget(gfx::VulkanContext &context);
};

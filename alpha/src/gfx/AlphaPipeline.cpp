#include "AlphaDrawData.h"
#include <stdexcept>
#include "spirv/AlphaWorldVertexShader.h"
#include "spirv/AlphaWorldPixelShader.h"
#include "spirv/AlphaSkyPixelShader.h"
#include "spirv/AlphaWaterPixelShader.h"
#include "spirv/AlphaCloudPixelShader.h"
#include "spirv/AlphaUnusedPixelShader.h"
#include "spirv/AlphaShapeVertexShader.h"
#include "spirv/AlphaShapePixelShader.h"
#include "spirv/AlphaSurfaceVertexShader.h"
#include "spirv/AlphaSurfacePixelShader.h"
#include "spirv/AlphaDownsample2PixelShader.h"
#include "spirv/AlphaDownsample3PixelShader.h"
#include "spirv/AlphaDownsample4PixelShader.h"
#include "spirv/AlphaHorizontalBlurPixelShader.h"
#include "spirv/AlphaVerticalBlurPixelShader.h"
#include "spirv/AlphaFixedVertexShader.h"
#include "spirv/AlphaFixedPixelShader.h"

namespace {
struct ShaderBinary {
    const uint32_t *words;
    size_t bytes;
};
constexpr ShaderBinary shaders[] = {
    {AlphaWorldVertexShader, sizeof(AlphaWorldVertexShader)},
    {AlphaWorldPixelShader, sizeof(AlphaWorldPixelShader)},
    {AlphaSkyPixelShader, sizeof(AlphaSkyPixelShader)},
    {AlphaWaterPixelShader, sizeof(AlphaWaterPixelShader)},
    {AlphaCloudPixelShader, sizeof(AlphaCloudPixelShader)},
    {AlphaUnusedPixelShader, sizeof(AlphaUnusedPixelShader)},
    {AlphaShapeVertexShader, sizeof(AlphaShapeVertexShader)},
    {AlphaShapePixelShader, sizeof(AlphaShapePixelShader)},
    {AlphaSurfaceVertexShader, sizeof(AlphaSurfaceVertexShader)},
    {AlphaSurfacePixelShader, sizeof(AlphaSurfacePixelShader)},
    {AlphaDownsample2PixelShader, sizeof(AlphaDownsample2PixelShader)},
    {AlphaDownsample3PixelShader, sizeof(AlphaDownsample3PixelShader)},
    {AlphaDownsample4PixelShader, sizeof(AlphaDownsample4PixelShader)},
    {AlphaHorizontalBlurPixelShader, sizeof(AlphaHorizontalBlurPixelShader)},
    {AlphaVerticalBlurPixelShader, sizeof(AlphaVerticalBlurPixelShader)},
    {AlphaFixedVertexShader, sizeof(AlphaFixedVertexShader)},
    {AlphaFixedPixelShader, sizeof(AlphaFixedPixelShader)},
};

static_assert(std::size(shaders) == size_t(AlphaShaderId::Count));

// Every Direct3D 9 state value has a Vulkan equivalent here, so no render state can stop a frame.
VkBlendFactor blendFactor(DWORD value)
{
    switch (value) {
    case D3DBLEND_ZERO:
        return VK_BLEND_FACTOR_ZERO;
    case D3DBLEND_SRCCOLOR:
        return VK_BLEND_FACTOR_SRC_COLOR;
    case D3DBLEND_INVSRCCOLOR:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case D3DBLEND_SRCALPHA:
        return VK_BLEND_FACTOR_SRC_ALPHA;
    case D3DBLEND_INVSRCALPHA:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case D3DBLEND_DESTALPHA:
        return VK_BLEND_FACTOR_DST_ALPHA;
    case D3DBLEND_INVDESTALPHA:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case D3DBLEND_DESTCOLOR:
        return VK_BLEND_FACTOR_DST_COLOR;
    case D3DBLEND_INVDESTCOLOR:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case D3DBLEND_SRCALPHASAT:
        return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    case D3DBLEND_BLENDFACTOR:
        return VK_BLEND_FACTOR_CONSTANT_COLOR;
    case D3DBLEND_INVBLENDFACTOR:
        return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    default: // D3DBLEND_ONE
        return VK_BLEND_FACTOR_ONE;
    }
}

VkBlendOp blendOp(DWORD value)
{
    switch (value) {
    case D3DBLENDOP_SUBTRACT:
        return VK_BLEND_OP_SUBTRACT;
    case D3DBLENDOP_REVSUBTRACT:
        return VK_BLEND_OP_REVERSE_SUBTRACT;
    case D3DBLENDOP_MIN:
        return VK_BLEND_OP_MIN;
    case D3DBLENDOP_MAX:
        return VK_BLEND_OP_MAX;
    default: // D3DBLENDOP_ADD
        return VK_BLEND_OP_ADD;
    }
}

VkCompareOp compareOp(DWORD value)
{
    switch (value) {
    case D3DCMP_NEVER:
        return VK_COMPARE_OP_NEVER;
    case D3DCMP_LESS:
        return VK_COMPARE_OP_LESS;
    case D3DCMP_EQUAL:
        return VK_COMPARE_OP_EQUAL;
    case D3DCMP_GREATER:
        return VK_COMPARE_OP_GREATER;
    case D3DCMP_NOTEQUAL:
        return VK_COMPARE_OP_NOT_EQUAL;
    case D3DCMP_GREATEREQUAL:
        return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case D3DCMP_ALWAYS:
        return VK_COMPARE_OP_ALWAYS;
    default: // D3DCMP_LESSEQUAL
        return VK_COMPARE_OP_LESS_OR_EQUAL;
    }
}

VkPrimitiveTopology topology(D3DPRIMITIVETYPE value)
{
    switch (value) {
    case D3DPT_POINTLIST:
        return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case D3DPT_LINELIST:
        return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case D3DPT_LINESTRIP:
        return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case D3DPT_TRIANGLEFAN:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
    case D3DPT_TRIANGLESTRIP:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    default:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}
}

VkPipeline AlphaRenderDevice::pipeline(VkDevice device, VkPipelineLayout layout, VkFormat colorFormat,
                                       VkFormat depthFormat, VkSampleCountFlagBits samples)
{
    draw->device = device;
    const auto &rs = draw->renderStates;
    const std::array<DWORD, 13> state = {rs[D3DRS_CULLMODE],         rs[D3DRS_ZENABLE],
                                         rs[D3DRS_ZWRITEENABLE],     rs[D3DRS_ZFUNC],
                                         rs[D3DRS_COLORWRITEENABLE], rs[D3DRS_ALPHABLENDENABLE],
                                         rs[D3DRS_BLENDOP],          rs[D3DRS_SRCBLEND],
                                         rs[D3DRS_DESTBLEND],        rs[D3DRS_SEPARATEALPHABLENDENABLE],
                                         rs[D3DRS_SRCBLENDALPHA],    rs[D3DRS_DESTBLENDALPHA],
                                         rs[D3DRS_BLENDOPALPHA]};
    DrawData::PipelineKey key {draw->vs, draw->ps,    draw->stride, draw->primitive,
                               state,    colorFormat, depthFormat,  samples};
    auto found = draw->pipelines.find(key);
    if (found != draw->pipelines.end())
        return found->second;
    auto module = [&](AlphaShaderId id) {
        auto i = size_t(id);
        auto &handle = draw->modules.at(i);
        if (!handle) {
            VkShaderModuleCreateInfo info {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            info.pCode = shaders[i].words;
            info.codeSize = shaders[i].bytes;
            if (vkCreateShaderModule(device, &info, nullptr, &handle) != VK_SUCCESS)
                throw std::runtime_error("Could not create Alpha Vulkan shader");
        }
        return handle;
    };
    VkPipelineShaderStageCreateInfo stages[] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                                                 VK_SHADER_STAGE_VERTEX_BIT, module(draw->vs), "main", nullptr},
                                                {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                                                 VK_SHADER_STAGE_FRAGMENT_BIT, module(draw->ps), "main", nullptr}};
    VkVertexInputAttributeDescription attributes[5] = {};
    uint32_t n = 0;
    auto attr = [&](uint32_t location, VkFormat format, uint32_t offset) {
        attributes[n++] = {location, 0, format, offset};
    };
    switch (draw->vs) {
    case AlphaShaderId::WorldVertexShader:
        attr(0, VK_FORMAT_R8G8B8A8_UINT, 0);
        attr(1, VK_FORMAT_R8G8B8A8_UNORM, 4);
        break;
    case AlphaShaderId::FixedVertexShader:
        attr(0, VK_FORMAT_R32G32B32_SFLOAT, 0);
        attr(1, VK_FORMAT_R8G8B8A8_UNORM, 12);
        attr(2, VK_FORMAT_R32G32_SFLOAT, draw->stride >= 24 ? 16 : 0);
        break;
    default:
        attr(0, VK_FORMAT_R32G32_SFLOAT, 0);
        attr(1, VK_FORMAT_R32G32B32A32_SFLOAT, 8);
        if (draw->vs == AlphaShaderId::ShapeVertexShader) {
            attr(2, VK_FORMAT_R32G32_SFLOAT, 24);
            attr(3, VK_FORMAT_R32G32_SFLOAT, 32);
        }
        attr(4, VK_FORMAT_R32G32_SFLOAT, 40);
        break;
    }
    VkVertexInputBindingDescription binding {0, draw->stride, VK_VERTEX_INPUT_RATE_VERTEX};
    VkPipelineVertexInputStateCreateInfo vertex {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex.vertexBindingDescriptionCount = 1;
    vertex.pVertexBindingDescriptions = &binding;
    vertex.vertexAttributeDescriptionCount = n;
    vertex.pVertexAttributeDescriptions = attributes;
    VkPipelineInputAssemblyStateCreateInfo assembly {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = topology(draw->primitive);
    VkPipelineViewportStateCreateInfo viewport {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.lineWidth = 1;
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.cullMode = rs[D3DRS_CULLMODE] == D3DCULL_NONE ? VK_CULL_MODE_NONE
                      : rs[D3DRS_CULLMODE] == D3DCULL_CW ? VK_CULL_MODE_FRONT_BIT
                                                         : VK_CULL_MODE_BACK_BIT;
    VkPipelineMultisampleStateCreateInfo msaa {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    msaa.rasterizationSamples = samples;
    VkPipelineDepthStencilStateCreateInfo depth {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = depthFormat && rs[D3DRS_ZENABLE];
    depth.depthWriteEnable = depth.depthTestEnable && rs[D3DRS_ZWRITEENABLE];
    depth.depthCompareOp = compareOp(rs[D3DRS_ZFUNC]);
    VkPipelineColorBlendAttachmentState attachment {};
    attachment.colorWriteMask = rs[D3DRS_COLORWRITEENABLE];
    attachment.blendEnable = rs[D3DRS_ALPHABLENDENABLE];
    attachment.colorBlendOp = blendOp(rs[D3DRS_BLENDOP]);
    attachment.srcColorBlendFactor = blendFactor(rs[D3DRS_SRCBLEND]);
    attachment.dstColorBlendFactor = blendFactor(rs[D3DRS_DESTBLEND]);
    attachment.srcAlphaBlendFactor =
        rs[D3DRS_SEPARATEALPHABLENDENABLE] ? blendFactor(rs[D3DRS_SRCBLENDALPHA]) : attachment.srcColorBlendFactor;
    attachment.dstAlphaBlendFactor =
        rs[D3DRS_SEPARATEALPHABLENDENABLE] ? blendFactor(rs[D3DRS_DESTBLENDALPHA]) : attachment.dstColorBlendFactor;
    attachment.alphaBlendOp =
        rs[D3DRS_SEPARATEALPHABLENDENABLE] ? blendOp(rs[D3DRS_BLENDOPALPHA]) : attachment.colorBlendOp;
    VkPipelineColorBlendStateCreateInfo blend {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;
    VkDynamicState dynamics[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamics;
    VkPipelineRenderingCreateInfo rendering {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &colorFormat;
    rendering.depthAttachmentFormat = depthFormat;
    VkGraphicsPipelineCreateInfo info {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.pNext = &rendering;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertex;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &msaa;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = layout;
    VkPipeline result;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &result) != VK_SUCCESS)
        throw std::runtime_error("Could not create Alpha Vulkan graphics pipeline");
    draw->pipelines.emplace(key, result);
    return result;
}

#include "AlphaDrawData.h"
#include "VulkanQuestIcons.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace {
template<class T> void bind(T *&slot, T *resource)
{
    if (resource)
        resource->retain();
    if (slot)
        slot->Release();
    slot = resource;
}

VkRect2D rectangle(LONG x, LONG y, LONG right, LONG bottom)
{
    return {{x, y}, {uint32_t(std::max(0L, right - x)), uint32_t(std::max(0L, bottom - y))}};
}

uint32_t vertexCount(D3DPRIMITIVETYPE type, UINT count)
{
    switch (type) {
    case D3DPT_TRIANGLELIST:
        return count * 3;
    case D3DPT_LINELIST:
        return count * 2;
    case D3DPT_TRIANGLEFAN:
    case D3DPT_TRIANGLESTRIP:
        return count ? count + 2 : 0;
    default:
        throw std::logic_error("Unsupported Alpha primitive");
    }
}

VkSamplerAddressMode addressMode(DWORD mode)
{
    switch (mode) {
    case D3DTADDRESS_BORDER:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    case D3DTADDRESS_CLAMP:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case D3DTADDRESS_MIRROR:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    default:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}
}

AlphaBuffer::AlphaBuffer(UINT size, VkBufferUsageFlags usage, VkIndexType type)
    : type(type),
      usage(usage),
      bytes(size)
{}

HRESULT AlphaBuffer::Lock(UINT offset, UINT size, void **out, DWORD)
{
    if (offset > bytes.size() || size > bytes.size() - offset)
        return E_INVALIDARG;
    *out = bytes.data() + offset;
    return S_OK;
}
HRESULT AlphaBuffer::Unlock()
{
    dirty = true;
    return S_OK;
}
const gfx::BufferPtr &AlphaBuffer::upload(gfx::VulkanContext &context)
{
    if (dirty) {
        buffer = context.createBuffer(bytes.data(), bytes.size(), usage);
        dirty = false;
    }
    return buffer;
}

AlphaTexture::AlphaTexture(UINT w, UINT h, D3DFORMAT format, AlphaSurface *target)
    : width(w),
      height(h),
      pixelBytes(format == D3DFMT_R8G8B8 ? 3 : 4),
      bytes(target ? 0 : size_t(w) * h * pixelBytes),
      surface(target)
{}
AlphaTexture::~AlphaTexture()
{
    if (surface)
        surface->Release();
}
HRESULT AlphaTexture::LockRect(UINT level, D3DLOCKED_RECT *out, const RECT *region, DWORD)
{
    if (level || region || surface)
        return E_INVALIDARG;
    out->Pitch = width * pixelBytes;
    out->pBits = bytes.data();
    return S_OK;
}
HRESULT AlphaTexture::UnlockRect(UINT)
{
    dirty = true;
    return S_OK;
}
HRESULT AlphaTexture::GetSurfaceLevel(UINT level, AlphaSurface **out)
{
    if (level || !surface)
        return E_INVALIDARG;
    surface->retain();
    *out = surface;
    return S_OK;
}
gfx::ImagePtr AlphaTexture::upload(gfx::VulkanContext &context)
{
    if (surface)
        return surface->image;
    if (dirty) {
        std::vector<unsigned char> rgba(size_t(width) * height * 4);
        for (size_t i = 0, j = 0; i < bytes.size(); i += pixelBytes, j += 4) {
            rgba[j] = bytes[i + 2];
            rgba[j + 1] = bytes[i + 1];
            rgba[j + 2] = bytes[i];
            rgba[j + 3] = pixelBytes == 4 ? bytes[i + 3] : 255;
        }
        image = context.createTexture(width, height, rgba.data());
        dirty = false;
    }
    return image;
}

AlphaRenderDevice::AlphaRenderDevice(HWND window)
    : window(window),
      draw(std::make_unique<DrawData>())
{
    std::ofstream log("log_vulkan.txt");
    context.initialize(
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
        [window](VkInstance instance, VkSurfaceKHR *surface) {
            VkWin32SurfaceCreateInfoKHR info {VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
            info.hinstance = GetModuleHandleW(nullptr);
            info.hwnd = window;
            return vkCreateWin32SurfaceKHR(instance, &info, nullptr, surface);
        },
        log);
    draw->renderStates[D3DRS_COLORWRITEENABLE] = 15;
    draw->renderStates[D3DRS_ZENABLE] = draw->renderStates[D3DRS_ZWRITEENABLE] = TRUE;
    draw->renderStates[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
    draw->renderStates[D3DRS_SRCBLEND] = draw->renderStates[D3DRS_SRCBLENDALPHA] = D3DBLEND_ONE;
    draw->renderStates[D3DRS_DESTBLEND] = draw->renderStates[D3DRS_DESTBLENDALPHA] = D3DBLEND_ZERO;
    draw->renderStates[D3DRS_CULLMODE] = D3DCULL_CCW;
    for (unsigned matrix : {47u, 51u, 55u})
        for (unsigned i = 0; i < 4; ++i)
            draw->constants[matrix + i][i] = 1;
}
AlphaRenderDevice::~AlphaRenderDevice()
{
    context.waitIdle();
}
AlphaRenderDevice::DrawData::~DrawData()
{
    for (auto *texture : textures)
        if (texture)
            texture->Release();
    if (vertices)
        vertices->Release();
    if (indices)
        indices->Release();
    for (const auto &entry : pipelines)
        vkDestroyPipeline(device, entry.second, nullptr);
    for (auto module : modules)
        if (module)
            vkDestroyShaderModule(device, module, nullptr);
}
bool AlphaRenderDevice::beginFrame()
{
    RECT r {};
    GetClientRect(window, &r);
    auto extent = context.beginFrame({uint32_t(r.right), uint32_t(r.bottom)}, false, samples, false, false);
    draw->extent = extent;
    draw->inFrame = extent.width != 0;
    return draw->inFrame;
}
void AlphaRenderDevice::present()
{
    if (context.present() && draw->firstFrame) {
        std::ofstream("log_vulkan.txt", std::ios::app) << "First Vulkan frame presented\n";
        draw->firstFrame = false;
    }
    draw->inFrame = false;
}
void AlphaRenderDevice::drawQuestMarkers(const std::vector<gfx::QuestMarker> &markers,
                                         const plasma::Vector<3, float> &center, const plasma::Matrix<float> &view,
                                         const plasma::Matrix<float> &projection, bool overlay, const RECT *clipRect)
{
    if (markers.empty() || !draw->inFrame)
        return;
    if (!draw->questIcons)
        draw->questIcons = gfx::loadQuestIcons(context);
    if (!draw->questIcons)
        return;
    const auto clipFromWorld = projection * view;
    auto &vertices = draw->markerVertices;
    auto &indices = draw->markerIndices;
    vertices.clear();
    indices.clear();
    for (const auto &marker : markers) {
        const auto p = marker.position - center;
        plasma::Vector<4, float> clip;
        for (int i = 0; i < 4; ++i)
            clip[i] = clipFromWorld[i] * p[0] + clipFromWorld[i + 4] * p[1] + clipFromWorld[i + 8] * p[2] +
                      clipFromWorld[i + 12];
        if (clip[3] <= 0 || clip[2] < 0 || clip[2] >= clip[3] || std::abs(clip[0]) > clip[3] ||
            std::abs(clip[1]) > clip[3])
            continue;
        if (clipRect) {
            const float x = (1 + clip[0] / clip[3]) * draw->extent.width * 0.5f;
            const float y = (1 - clip[1] / clip[3]) * draw->extent.height * 0.5f;
            if (x - 12 < clipRect->left || x + 12 > clipRect->right || y - 26 < clipRect->top || y > clipRect->bottom)
                continue;
        }
        gfx::appendQuestIcon(marker.icon, clip, draw->extent.width, draw->extent.height, vertices, indices);
    }
    if (indices.empty())
        return;
    auto &uniforms = draw->markerUniforms;
    uniforms.world.identity();
    uniforms.view.identity();
    uniforms.projection.identity();
    uniforms.textureEnabled = 1;
    context.setDrawState({gfx::VulkanProgram::Textured, gfx::RasterizerTwoSided,
                          overlay ? gfx::DepthDisabled : gfx::DepthTestNoWrite, gfx::BlendAlpha},
                         &uniforms, sizeof(uniforms));
    context.bindTexture(0, draw->questIcons);
    context.drawTransient(vertices.data(), vertices.size() * sizeof(vertices.front()), indices.data(),
                          uint32_t(indices.size()));
}

void AlphaRenderDevice::snapshotWater(double originX, double originY, float brightness, bool submerged, float projZ,
                                      float projW)
{
    context.snapshotWaterBackground();
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    constexpr double turn = 6.283185307179586;
    auto &surface = draw->constants[59];
    surface[0] = float(std::remainder(-0.65 * originX - 0.31 * originY + 0.8 * seconds, turn));
    surface[1] = float(std::remainder(0.27 * originX - 0.72 * originY - 0.65 * seconds, turn));
    surface[2] = brightness;
    surface[3] = submerged ? 1.f : 0.f;
    draw->constants[60][0] = projZ;
    draw->constants[60][1] = projW;
}
HRESULT AlphaRenderDevice::CreateVertexBuffer(UINT size, DWORD, DWORD, D3DPOOL, AlphaBuffer **out, HANDLE *)
{
    *out = new AlphaBuffer(size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_INDEX_TYPE_UINT16);
    return S_OK;
}
HRESULT AlphaRenderDevice::CreateIndexBuffer(UINT size, DWORD, D3DFORMAT format, D3DPOOL, AlphaBuffer **out, HANDLE *)
{
    *out = new AlphaBuffer(size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                           format == D3DFMT_INDEX32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
    return S_OK;
}
HRESULT AlphaRenderDevice::CreateTexture(UINT w, UINT h, UINT, DWORD usage, D3DFORMAT format, D3DPOOL,
                                         AlphaTexture **out, HANDLE *)
{
    AlphaSurface *surface = nullptr;
    if (usage & D3DUSAGE_RENDERTARGET) {
        auto owner = std::make_unique<AlphaSurface>();
        owner->image = context.createSurface(w, h, false);
        surface = owner.release();
    }
    *out = new AlphaTexture(w, h, format, surface);
    return S_OK;
}
HRESULT AlphaRenderDevice::CreateDepthStencilSurface(UINT w, UINT h, D3DFORMAT, D3DMULTISAMPLE_TYPE, DWORD, BOOL,
                                                     AlphaSurface **out, HANDLE *)
{
    auto surface = std::make_unique<AlphaSurface>();
    surface->image = context.createSurface(w, h, true);
    *out = surface.release();
    return S_OK;
}
void AlphaRenderDevice::GetRenderTarget(UINT, AlphaSurface **out)
{
    *out = draw->color;
    (*out)->retain();
}
void AlphaRenderDevice::GetDepthStencilSurface(AlphaSurface **out)
{
    *out = draw->depth;
    if (*out)
        (*out)->retain();
}
void AlphaRenderDevice::SetRenderTarget(UINT, AlphaSurface *s)
{
    draw->color = s;
    draw->targetChanged = true;
}
void AlphaRenderDevice::SetDepthStencilSurface(AlphaSurface *s)
{
    draw->depth = s;
    draw->targetChanged = true;
}
void AlphaRenderDevice::DrawData::applyTarget(gfx::VulkanContext &context)
{
    if (!targetChanged || !inFrame)
        return;
    if (targets.back() != color) {
        auto it = std::find(targets.begin(), targets.end(), color);
        if (it != targets.end()) {
            while (targets.back() != color) {
                context.endSurface();
                targets.pop_back();
            }
        } else {
            context.beginSurface(color->image, depth ? depth->image : gfx::ImagePtr {});
            targets.push_back(color);
        }
    }
    targetChanged = false;
}
HRESULT AlphaRenderDevice::CreateVertexDeclaration(const D3DVERTEXELEMENT9 *, AlphaVertexLayout **out)
{
    *out = new AlphaVertexLayout;
    return S_OK;
}
HRESULT AlphaRenderDevice::CreateVertexShader(AlphaShaderId id, AlphaShader **out)
{
    *out = new AlphaShader(id);
    return S_OK;
}
HRESULT AlphaRenderDevice::CreatePixelShader(AlphaShaderId id, AlphaShader **out)
{
    *out = new AlphaShader(id);
    return S_OK;
}
void AlphaRenderDevice::SetVertexShader(AlphaShader *s)
{
    draw->vs = s ? s->id : AlphaShaderId::FixedVertexShader;
}
void AlphaRenderDevice::SetPixelShader(AlphaShader *s)
{
    draw->ps = s ? s->id : AlphaShaderId::FixedPixelShader;
}
void AlphaRenderDevice::SetVertexDeclaration(AlphaVertexLayout *) {}
void AlphaRenderDevice::SetFVF(DWORD) {}
void AlphaRenderDevice::SetVertexShaderConstantF(UINT i, const float *v, UINT n)
{
    if (i <= 40 && n <= 40 - i)
        std::memcpy(draw->constants[i], v, n * 16);
}
void AlphaRenderDevice::SetPixelShaderConstantF(UINT i, const float *v, UINT n)
{
    if (i < 5 && n <= 5 - i)
        std::memcpy(draw->constants[40 + i], v, n * 16);
}
void AlphaRenderDevice::SetVertexShaderConstantB(UINT i, const BOOL *v, UINT n)
{
    if (i == 0 && n == 1)
        std::memcpy(draw->constants[45], v, 4);
}
void AlphaRenderDevice::SetPixelShaderConstantB(UINT i, const BOOL *v, UINT n)
{
    if (i == 0 && n == 1)
        std::memcpy(draw->constants[46], v, 4);
}
void AlphaRenderDevice::SetRenderState(D3DRENDERSTATETYPE type, DWORD value)
{
    draw->renderStates.at(type) = value;
}
void AlphaRenderDevice::SetSamplerState(UINT stage, D3DSAMPLERSTATETYPE type, DWORD value)
{
    auto &s = draw->samplers.at(stage);
    if (type == D3DSAMP_ADDRESSU)
        s.u = addressMode(value);
    if (type == D3DSAMP_ADDRESSV)
        s.v = addressMode(value);
    if (type == D3DSAMP_MAGFILTER)
        s.mag = value == D3DTEXF_POINT ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    if (type == D3DSAMP_MINFILTER)
        s.min = value == D3DTEXF_POINT ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
}
void AlphaRenderDevice::SetTexture(UINT stage, AlphaTexture *t)
{
    bind(draw->textures.at(stage), t);
}
void AlphaRenderDevice::SetStreamSource(UINT, AlphaBuffer *v, UINT offset, UINT stride)
{
    bind(draw->vertices, v);
    draw->streamStride = stride;
    draw->offset = offset;
}
void AlphaRenderDevice::SetIndices(AlphaBuffer *v)
{
    bind(draw->indices, v);
}
void AlphaRenderDevice::SetTransform(D3DTRANSFORMSTATETYPE type, const D3DMATRIX *m)
{
    unsigned slot = type == D3DTS_WORLD ? 47 : type == D3DTS_VIEW ? 51 : 55;
    std::memcpy(draw->constants[slot], m, sizeof(*m));
}
void AlphaRenderDevice::SetScissorRect(const RECT *r)
{
    draw->scissor = rectangle(r->left, r->top, r->right, r->bottom);
}
void AlphaRenderDevice::Clear(DWORD count, const D3DRECT *areas, DWORD flags, D3DCOLOR packed, float depth, DWORD)
{
    draw->applyTarget(context);
    VkClearColorValue color {};
    const unsigned shifts[] = {16, 8, 0, 24};
    for (unsigned i = 0; i < 4; ++i)
        color.float32[i] = ((packed >> shifts[i]) & 255) / 255.f;
    for (DWORD i = 0; i < std::max<DWORD>(count, 1); ++i) {
        VkRect2D area {};
        if (count)
            area = rectangle(areas[i].x1, areas[i].y1, areas[i].x2, areas[i].y2);
        context.clearAttachments((flags & D3DCLEAR_TARGET) ? &color : nullptr,
                                 (flags & D3DCLEAR_ZBUFFER) ? &depth : nullptr, count ? &area : nullptr);
    }
}
void AlphaRenderDevice::prepareDraw(D3DPRIMITIVETYPE type, unsigned stride)
{
    draw->applyTarget(context);
    draw->primitive = type;
    draw->stride = stride;
    for (unsigned i = 0; i < 2; ++i) {
        auto &s = draw->samplers[i];
        context.setTextureSampler(i, s.min, s.mag, s.u, s.v);
        context.bindTexture(i, draw->textures[i] ? draw->textures[i]->upload(context) : gfx::ImagePtr {});
    }
    if (draw->ps == AlphaShaderId::WaterPixelShader)
        context.bindWaterBackground();
    context.setDrawState(*this, draw->constants, sizeof(draw->constants),
                         draw->renderStates[D3DRS_SCISSORTESTENABLE] ? &draw->scissor : nullptr);
}
void AlphaRenderDevice::DrawIndexedPrimitive(D3DPRIMITIVETYPE type, INT base, UINT, UINT, UINT first, UINT count)
{
    if (!draw->inFrame || !draw->vertices || !draw->indices || !draw->streamStride || !count)
        return;
    prepareDraw(type, draw->streamStride);
    context.bindVertex(draw->vertices->upload(context));
    context.bindIndex(draw->indices->upload(context), draw->indices->type);
    context.drawIndexed(vertexCount(type, count), first, base + int(draw->offset / draw->stride));
}
void AlphaRenderDevice::DrawPrimitiveUP(D3DPRIMITIVETYPE type, UINT count, const void *data, UINT stride)
{
    if (!draw->inFrame || !count)
        return;
    const auto n = vertexCount(type, count);
    auto &indices = draw->transientIndices;
    if (indices.size() < n) {
        const size_t first = indices.size();
        indices.resize(n);
        for (size_t i = first; i < n; ++i)
            indices[i] = uint32_t(i);
    }
    prepareDraw(type, stride);
    context.drawTransient(data, size_t(n) * stride, indices.data(), n);
}

#pragma once

#include <windows.h>
#include <d3d9types.h> // Original vertex layouts, packed colors and render-state identifiers.
#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include "gfx/VulkanContext.h"
#include "gfx/QuestMarker.h"
#include "plasma/Matrix.h"

enum class AlphaShaderId {
    WorldVertexShader,
    WorldPixelShader,
    SkyPixelShader,
    WaterPixelShader,
    CloudPixelShader,
    UnusedPixelShader,
    ShapeVertexShader,
    ShapePixelShader,
    SurfaceVertexShader,
    SurfacePixelShader,
    Downsample2PixelShader,
    Downsample3PixelShader,
    Downsample4PixelShader,
    HorizontalBlurPixelShader,
    VerticalBlurPixelShader,
    FixedVertexShader,
    FixedPixelShader,
    Count
};

struct AlphaResource {
    void retain() { ++references; }
    void Release()
    {
        if (--references == 0)
            delete this;
    }

protected:
    virtual ~AlphaResource() = default;

private:
    std::atomic<unsigned> references {1};
};

struct AlphaShader : AlphaResource {
    explicit AlphaShader(AlphaShaderId id)
        : id(id)
    {}
    AlphaShaderId id;
};

struct AlphaVertexLayout : AlphaResource {};

struct AlphaBuffer : AlphaResource {
    AlphaBuffer(UINT size, VkBufferUsageFlags usage, VkIndexType type);
    HRESULT Lock(UINT offset, UINT size, void **out, DWORD flags);
    HRESULT Unlock();
    const gfx::BufferPtr &upload(gfx::VulkanContext &context);
    VkIndexType type;

private:
    VkBufferUsageFlags usage;
    std::vector<unsigned char> bytes;
    gfx::BufferPtr buffer;
    bool dirty = true;
};

struct AlphaSurface : AlphaResource {
    gfx::ImagePtr image;
};

struct AlphaTexture : AlphaResource {
    AlphaTexture(UINT width, UINT height, D3DFORMAT format, AlphaSurface *surface);
    ~AlphaTexture() override;
    HRESULT LockRect(UINT level, D3DLOCKED_RECT *out, const RECT *region, DWORD flags);
    HRESULT UnlockRect(UINT level);
    HRESULT GetSurfaceLevel(UINT level, AlphaSurface **out);
    gfx::ImagePtr upload(gfx::VulkanContext &context);

private:
    UINT width, height, pixelBytes;
    std::vector<unsigned char> bytes;
    bool dirty = true;
    gfx::ImagePtr image;
    AlphaSurface *surface;
};

struct AlphaRenderDevice : gfx::VulkanPipelineSource {
    explicit AlphaRenderDevice(HWND window);
    ~AlphaRenderDevice();
    void Release() { delete this; }
    bool beginFrame();
    void present();
    void setSamples(unsigned value) { samples = value; }
    void drawQuestMarkers(const std::vector<gfx::QuestMarker> &markers, const plasma::Vector<3, float> &center,
                          const plasma::Matrix<float> &view, const plasma::Matrix<float> &projection, bool overlay,
                          const RECT *clipRect = nullptr);
    void snapshotWater(double originX, double originY, float brightness, bool submerged, float projZ, float projW);
    HRESULT CreateVertexBuffer(UINT size, DWORD, DWORD, D3DPOOL, AlphaBuffer **out, HANDLE *);
    HRESULT CreateIndexBuffer(UINT size, DWORD, D3DFORMAT format, D3DPOOL, AlphaBuffer **out, HANDLE *);
    HRESULT CreateTexture(UINT width, UINT height, UINT, DWORD usage, D3DFORMAT format, D3DPOOL, AlphaTexture **out,
                          HANDLE *);
    HRESULT CreateDepthStencilSurface(UINT width, UINT height, D3DFORMAT, D3DMULTISAMPLE_TYPE, DWORD, BOOL,
                                      AlphaSurface **out, HANDLE *);
    void GetRenderTarget(UINT, AlphaSurface **out);
    void GetDepthStencilSurface(AlphaSurface **out);
    void SetRenderTarget(UINT, AlphaSurface *surface);
    void SetDepthStencilSurface(AlphaSurface *surface);
    HRESULT CreateVertexDeclaration(const D3DVERTEXELEMENT9 *, AlphaVertexLayout **out);
    HRESULT CreateVertexShader(AlphaShaderId id, AlphaShader **out);
    HRESULT CreatePixelShader(AlphaShaderId id, AlphaShader **out);
    void SetVertexShader(AlphaShader *value);
    void SetPixelShader(AlphaShader *value);
    void SetVertexDeclaration(AlphaVertexLayout *);
    void SetVertexShaderConstantF(UINT index, const float *value, UINT count);
    void SetPixelShaderConstantF(UINT index, const float *value, UINT count);
    void SetPixelShaderConstantB(UINT index, const BOOL *value, UINT count);
    void SetVertexShaderConstantB(UINT index, const BOOL *value, UINT count);
    void SetRenderState(D3DRENDERSTATETYPE type, DWORD value);
    void SetSamplerState(UINT stage, D3DSAMPLERSTATETYPE type, DWORD value);
    void SetTexture(UINT stage, AlphaTexture *texture);
    void SetStreamSource(UINT, AlphaBuffer *buffer, UINT offset, UINT stride);
    void SetIndices(AlphaBuffer *buffer);
    void SetFVF(DWORD);
    void SetTransform(D3DTRANSFORMSTATETYPE type, const D3DMATRIX *matrix);
    void SetScissorRect(const RECT *rect);
    void Clear(DWORD numRects, const D3DRECT *rects, DWORD flags, D3DCOLOR color, float depth, DWORD stencil);
    void DrawIndexedPrimitive(D3DPRIMITIVETYPE type, INT base, UINT, UINT, UINT first, UINT count);
    void DrawPrimitiveUP(D3DPRIMITIVETYPE type, UINT count, const void *vertices, UINT stride);

private:
    struct DrawData;
    void prepareDraw(D3DPRIMITIVETYPE type, unsigned stride);
    VkPipeline pipeline(VkDevice device, VkPipelineLayout layout, VkFormat color, VkFormat depth,
                        VkSampleCountFlagBits samples) override;
    HWND window;
    unsigned samples = 1;
    gfx::VulkanContext context;
    std::unique_ptr<DrawData> draw;
};

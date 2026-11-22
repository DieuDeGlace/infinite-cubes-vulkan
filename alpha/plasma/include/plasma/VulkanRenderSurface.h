#pragma once
#include "gfx/AlphaRenderDevice.h"
#include "plasma/RenderSurface.h"

namespace plasma {
class VulkanEngine;

class VulkanRenderSurface : public RenderSurface {
public:
    VulkanRenderSurface(VulkanEngine *engine, bool depthAttachment);
    virtual ~VulkanRenderSurface();
    virtual bool resize(int width, int height);
    virtual void begin();
    virtual void end();
    virtual void bindTexture();
    virtual void draw(const Vector<2, float> &position, const Vector<2, float> &size,
                      const Vector<2, float> &texturePosition, float scale);
    virtual void drawBlurred(const Vector<2, float> &, const Vector<2, float> &, float, bool, float);
    virtual void drawDownsampled(const Vector<2, float> &, const Vector<2, float> &, const Vector<2, float> &, float,
                                 int);
    virtual void clearRect(const Vector<2, float> &, const Vector<2, float> &, bool);
    virtual void clear();
    void releaseDeviceResources();

private:
    int m_width;
    int m_height;
    VulkanEngine *m_engine;
    AlphaTexture *m_colorTexture;
    AlphaSurface *m_colorSurface;
    AlphaSurface *m_depthSurface;
    AlphaSurface *m_previousColorSurface; // retained between Begin and End
    AlphaSurface *m_previousDepthSurface;
};
}

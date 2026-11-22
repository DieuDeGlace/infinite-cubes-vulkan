#pragma once

#include "gfx/VulkanContext.h"
#include "plasma/RenderSurface.h"

namespace plasma {

class VulkanEngine;

class VulkanRenderSurface final : public RenderSurface {
public:
    VulkanRenderSurface(VulkanEngine *engine, bool depthAttachment);
    bool resize(int width, int height) override;
    void begin() override;
    void end() override;
    void bindTexture() override;
    void draw(const Vector<2, float> &position, const Vector<2, float> &size,
              const Vector<2, float> &texturePosition, float scale) override;
    void drawBlurred(const Vector<2, float> &position, const Vector<2, float> &size, float radius,
                     bool horizontal, float scale) override;
    void drawDownsampled(const Vector<2, float> &position, const Vector<2, float> &size,
                         const Vector<2, float> &texturePosition, float scale, int factor) override;

private:
    VulkanEngine *m_engine;
    int m_width = 0;
    int m_height = 0;
    gfx::ImagePtr m_color;
    gfx::ImagePtr m_depth;
};

}

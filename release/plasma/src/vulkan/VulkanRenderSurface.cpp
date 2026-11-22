#include "plasma/VulkanRenderSurface.h"

#include "plasma/VulkanEngine.h"

namespace plasma {

VulkanRenderSurface::VulkanRenderSurface(VulkanEngine *engine, bool depthAttachment)
    : RenderSurface(depthAttachment), m_engine(engine)
{}

void VulkanRenderSurface::bindTexture()
{
    m_engine->m_context.bindTexture(0, m_color);
}

void VulkanRenderSurface::begin()
{
    if (m_color)
        m_engine->m_context.beginSurface(m_color, m_depth);
}

void VulkanRenderSurface::end()
{
    if (m_color)
        m_engine->m_context.endSurface();
}

bool VulkanRenderSurface::resize(int width, int height)
{
    if (width <= 0 || height <= 0 || (m_color && width == m_width && height == m_height))
        return false;
    auto &context = m_engine->m_context;
    auto color = context.createSurface(width, height, false);
    gfx::ImagePtr depth;
    if (m_depthAttachment)
        depth = context.createSurface(width, height, true);
    m_color = std::move(color);
    m_depth = std::move(depth);
    m_width = width;
    m_height = height;
    return true;
}

void VulkanRenderSurface::draw(const Vector<2, float> &position, const Vector<2, float> &size,
                              const Vector<2, float> &texturePosition, float scale)
{
    if (!m_color)
        return;
    Matrix<float> modelView;
    m_engine->getModelViewMatrix(modelView);
    // The fraction of a pixel the translation has after scaling; dropped at the surface's own scale.
    float textureScale = scale;
    Vector<2, float> fraction(modelView.m_elements[12] * scale, modelView.m_elements[13] * scale);
    fraction = fraction - Vector<2, float>(Vector<2, int>(fraction));
    if (scale == 1.0f) {
        fraction[0] = 0.0f;
        fraction[1] = 0.0f;
    }
    Matrix<float> identity;
    identity.identity();
    m_engine->loadMatrix(identity);
    // A negative size component means the surface's size.
    Vector<2, float> drawPosition = position;
    Vector<2, float> drawSize = size;
    if (size.m_elements[0] < 0.0f)
        drawSize.m_elements[0] = m_width;
    if (drawSize.m_elements[1] < 0.0f)
        drawSize.m_elements[1] = m_height;
    float width = m_width;
    float margin = (1.0f - scale) * 0.5f;
    float height = m_height;
    float marginX = margin * width;
    float marginY = margin * height;
    if (scale < 0.0f) {
        fraction[0] = 0.0f;
        textureScale = width / size.m_elements[0];
        fraction[1] = 0.0f;
        marginX = 0.0f;
        marginY = 0.0f;
    }
    Vector<2, float> textureOrigin((textureScale * texturePosition.m_elements[0] + (marginX - fraction[0])) / width,
                                   (textureScale * texturePosition.m_elements[1] + (marginY - fraction[1])) / height);
    Vector<2, float> textureSize(drawSize.m_elements[0] * textureScale / width,
                                 drawSize.m_elements[1] * textureScale / height);
    m_engine->applyRenderState();
    m_engine->setTextureEnabled(true);
    m_engine->setFilter(FILTER_NONE);
    m_engine->setTextureAdjustments(1.0f, 1.0f, 1.0f, 1.0f);
    m_engine->setWidgetDeformationEnabled(false);
    m_engine->setProjectionMatrix(m_engine->m_projection);
    m_engine->m_context.bindTexture(1, m_color);
    m_engine->drawTexturedRectangle(drawPosition, drawSize, textureOrigin, textureSize, m_drawColor);
    m_engine->loadMatrix(modelView);
}

void VulkanRenderSurface::drawBlurred(const Vector<2, float> &position, const Vector<2, float> &size, float radius,
                                     bool horizontal, float scale)
{
    draw(position, size, Vector<2, float>(0.0f, 0.0f), 1.0f);
}

void VulkanRenderSurface::drawDownsampled(const Vector<2, float> &position, const Vector<2, float> &size,
                                         const Vector<2, float> &texturePosition, float scale, int factor)
{
    draw(position, size, Vector<2, float>(0.0f, 0.0f), 1.0f);
}

}

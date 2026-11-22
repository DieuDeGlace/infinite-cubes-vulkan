#include "plasma/VulkanEngine.h"

#include "plasma/Display.h"
#include "plasma/Node.h"
#include "plasma/Transformation.h"
#include "plasma/VulkanDrawing.h"
#include "plasma/VulkanRenderSurface.h"

namespace plasma {

static const float UIOrthographicFarClip = 10000000.0f;

Texture *createVulkanTexture(Engine *engine, gfx::VulkanContext &context, int width, int height,
                             const unsigned char *pixels, const TextureFormat &format,
                             const std::wstring &name, bool transient);

VulkanEngine::VulkanEngine(gfx::VulkanContext &context) : m_context(context)
{
    m_modelView.identity();
    m_projection.identity();
    m_uniforms.projection.identity();
    m_uniforms.modelView.identity();
    m_uniforms.mask.identity();
    m_uniforms.texture.identity();
    m_uniforms.widgetBind.identity();
    m_uniforms.inverseWidgetBind.identity();

    // Each vertex packs position and UV in the first vector and color in the second.
    const Vector<4, float> vertices[] = {
        {0, 0, 0, 0}, {1, 1, 1, 1},
        {1, 0, 1, 0}, {1, 1, 1, 1},
        {1, 1, 1, 1}, {1, 1, 1, 1},
        {0, 1, 0, 1}, {1, 1, 1, 1},
    };
    const uint32_t indices[] = {0, 1, 2, 0, 3, 2};
    m_quadVertices = m_context.createBuffer(vertices, sizeof(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    m_quadIndices = m_context.createBuffer(indices, sizeof(indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
}

void VulkanEngine::applyRenderState()
{
    m_context.setDrawState({gfx::VulkanProgram::Interface, gfx::RasterizerTwoSided,
                            gfx::DepthDisabled, gfx::BlendAlpha}, &m_uniforms, sizeof(m_uniforms));
}

void VulkanEngine::drawQuad()
{
    m_context.bindVertex(m_quadVertices);
    m_context.bindIndex(m_quadIndices, VK_INDEX_TYPE_UINT32);
    m_context.drawIndexed(6);
}

void VulkanEngine::beginWindowDrawing()
{
    m_modelView.identity();
    m_projection.setOrthographic(0.0f, (float)m_viewportWidth, (float)m_viewportHeight, 0.0f, -1.0f,
                                 UIOrthographicFarClip);
}

void VulkanEngine::pushNodeMatrix(Node *node)
{
    m_modelViewStack.push_back(m_modelView);
    m_modelView *= node->m_transformation->m_matrix;
}

void VulkanEngine::beginTextDrawing(Node *node)
{
    applyRenderState();
    if (node && m_filterSurface &&
        (!(node->m_display->m_flags & (1 << DISPLAY_BLUR)) ||
         node->m_display->m_clipping.value() != DISPLAY_CLIPPING_DISABLED))
        m_filterSurface->bindTexture();
    setTextureEnabled(true);
    // The pixel shader's filter, chosen as in VulkanDrawing::draw.
    int filter = FILTER_NONE;
    if (node && node->m_display->m_clipping.value() != DISPLAY_CLIPPING_DISABLED) {
        if (node->m_display->m_flags & (1 << DISPLAY_MODULATION))
            filter = FILTER_MODULATION;
        else if (node->m_display->m_flags & (1 << DISPLAY_SCREEN))
            filter = FILTER_SCREEN;
        else
            filter = FILTER_CLIP;
    }
    setFilter(filter);
    setTextureAdjustments(1.0f, 1.0f, 1.0f, 1.0f);
    setWidgetDeformationEnabled(false);
    setProjectionMatrix(m_projection);
}

void VulkanEngine::beginSurface(RenderSurface *surface, const Vector<2, float> &position, const Vector<2, float> &size,
                               float scale, bool alignPixels)
{
    pushSurface(surface);
    surface->begin();
    if (alignPixels) {
        Vector<2, float> fraction(m_modelView.m_elements[12], m_modelView.m_elements[13]);
        fraction = fraction * scale;
        fraction = fraction - Vector<2, float>(Vector<2, int>(fraction));
        if (scale == 1.0f)
            fraction = Vector<2, float>(0.0f, 0.0f);
        m_projectionStack.push_back(m_projection);
        Vector<2, float> minimum = position - size * ((1.0f - scale) * 0.5f);
        m_projection.setOrthographic(minimum[0], size[0] + minimum[0], size[1] + minimum[1], minimum[1], -1.0f,
                                     UIOrthographicFarClip);
        m_projection.translate(-fraction[0], -fraction[1], 0.0f);
        m_projection.scale(scale, scale, 1.0f);
    } else {
        m_modelView.m_elements[12] = (float)(int)(m_modelView.m_elements[12] + 0.5f);
        m_modelView.m_elements[13] = (float)(int)(m_modelView.m_elements[13] + 0.5f);
        m_projectionStack.push_back(m_projection);
        m_projection.setOrthographic(position[0], position[0] + size[0], position[1] + size[1], position[1], -1.0f,
                                     UIOrthographicFarClip);
        m_projection.scale(scale, scale, 1.0f);
    }
}

Drawing *VulkanEngine::createDrawing()
{
    return new VulkanDrawing(this);
}

RenderSurface *VulkanEngine::createRenderSurface(bool depthAttachment)
{
    return new VulkanRenderSurface(this, depthAttachment);
}

Texture *VulkanEngine::createTexture(int width, int height, const unsigned char *pixels, const TextureFormat &format,
                                    const std::wstring &name, bool transient)
{
    return createVulkanTexture(this, m_context, width, height, pixels, format, name, transient);
}

void VulkanEngine::drawCaret(const Vector<2, float> &position, const Vector<2, float> &size,
                            const Vector<4, float> &color)
{
    if ((m_caretClock / 500 & 1) != 0) {
        Matrix<float> matrix = m_modelView;
        matrix.translate(position);
        matrix.scale(size[0], size[1]);
        setModelViewMatrix(matrix);
        setTextureEnabled(false);
        setColor(color);
        drawQuad();
    }
}

void VulkanEngine::drawTexturedRectangle(const Vector<2, float> &position, const Vector<2, float> &size,
                                        const Vector<2, float> &texturePosition, const Vector<2, float> &textureSize,
                                        const Vector<4, float> &color)
{
    Matrix<float> matrix = m_modelView;
    matrix.translate(position);
    matrix.scale(size[0], size[1]);
    setModelViewMatrix(matrix);
    Matrix<float> textureMatrix;
    textureMatrix.identity();
    textureMatrix.translate(texturePosition);
    textureMatrix.scale(textureSize[0], textureSize[1]);
    setTextureMatrix(textureMatrix);
    setColor(color);
    drawQuad();
}

void VulkanEngine::fillRectangle(const Vector<2, float> &position, const Vector<2, float> &size,
                                const Vector<4, float> &color)
{
    Matrix<float> matrix = m_modelView;
    matrix.translate(position);
    matrix.scale(size[0], size[1]);
    setModelViewMatrix(matrix);
    setTextureEnabled(false);
    setColor(color);
    drawQuad();
}

void VulkanEngine::popNodeMatrix(Node *node)
{
    if (!m_modelViewStack.empty()) {
        m_modelView = m_modelViewStack.back();
        m_modelViewStack.pop_back();
    }
}

void VulkanEngine::endSurface()
{
    m_surfaceStack.back()->end();
    popSurface();
    if (!m_projectionStack.empty()) {
        m_projection = m_projectionStack.back();
        m_projectionStack.pop_back();
    }
}

void VulkanEngine::getModelViewMatrix(Matrix<float> &matrix)
{
    matrix = m_modelView;
}

void VulkanEngine::loadMatrix(Matrix<float> &matrix)
{
    m_modelView = matrix;
}

void VulkanEngine::setProjectionMatrix(const Matrix<float> &matrix)
{
    m_uniforms.projection = matrix;
    m_uniforms.projection.transpose();
}

void VulkanEngine::setModelViewMatrix(const Matrix<float> &matrix)
{
    m_uniforms.modelView = matrix;
    m_uniforms.modelView.transpose();
}

void VulkanEngine::setPixelScaledModelViewMatrix(const Matrix<float> &matrix)
{
    m_uniforms.mask = matrix;
    m_uniforms.mask.transpose();
}

void VulkanEngine::setTextureMatrix(const Matrix<float> &matrix)
{
    m_uniforms.texture = matrix;
    m_uniforms.texture.transpose();
}

void VulkanEngine::setWidgetBindMatrix(const Matrix<float> &matrix)
{
    m_uniforms.widgetBind = matrix;
    m_uniforms.widgetBind.transpose();
}

void VulkanEngine::setInverseWidgetBindMatrix(const Matrix<float> &matrix)
{
    m_uniforms.inverseWidgetBind = matrix;
    m_uniforms.inverseWidgetBind.transpose();
}

void VulkanEngine::setWidgetBindPos(const Vector<2, float> &value)
{
    m_uniforms.widgetBindPos = value;
}

void VulkanEngine::setWidgetBindSize(const Vector<2, float> &value)
{
    m_uniforms.widgetBindSize = value;
}

void VulkanEngine::setDeformedWidgetPos(const Vector<2, float> &value)
{
    m_uniforms.deformedWidgetPos = value;
}

void VulkanEngine::setDeformedWidgetSize(const Vector<2, float> &value)
{
    m_uniforms.deformedWidgetSize = value;
}

void VulkanEngine::setColor(const Vector<4, float> &color)
{
    m_uniforms.color = color;
}

void VulkanEngine::setTextureEnabled(bool enabled)
{
    m_uniforms.textureEnabled = enabled;
}

void VulkanEngine::setFilter(int filter)
{
    m_uniforms.filter = filter;
}

void VulkanEngine::setTextureAdjustments(float opacity, float brightness, float contrast, float saturation)
{
    m_uniforms.textureAdjustments = Vector<4, float>(opacity, brightness, contrast, saturation);
}

void VulkanEngine::setWidgetDeformationEnabled(bool enabled)
{
    m_uniforms.widgetDeformation = enabled;
}

}

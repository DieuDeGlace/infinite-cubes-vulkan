#pragma once

#include <cstdint>
#include <list>
#include "gfx/VulkanContext.h"
#include "plasma/Engine.h"
#include "plasma/Matrix.h"

namespace plasma {

enum FilterMode {
    FILTER_NONE = 0,
    FILTER_CLIP = 1,
    FILTER_MODULATION = 2,
    FILTER_SCREEN = 3,
};

class VulkanEngine final : public Engine {
public:
    explicit VulkanEngine(gfx::VulkanContext &context);

    Texture *createTexture(int width, int height, const unsigned char *pixels, const TextureFormat &format,
                           const std::wstring &name = L"", bool transient = false) override;
    void drawCaret(const Vector<2, float> &position, const Vector<2, float> &size,
                   const Vector<4, float> &color) override;
    void drawTexturedRectangle(const Vector<2, float> &position, const Vector<2, float> &size,
                               const Vector<2, float> &texturePosition, const Vector<2, float> &textureSize,
                               const Vector<4, float> &color) override;
    void fillRectangle(const Vector<2, float> &position, const Vector<2, float> &size,
                       const Vector<4, float> &color) override;
    Drawing *createDrawing() override;
    RenderSurface *createRenderSurface(bool depthAttachment) override;
    void getModelViewMatrix(Matrix<float> &matrix) override;
    void loadMatrix(Matrix<float> &matrix) override;
    void applyRenderState() override;
    void beginTextDrawing(Node *node) override;
    void beginWindowDrawing() override;
    void pushNodeMatrix(Node *node) override;
    void popNodeMatrix(Node *node) override;
    void beginSurface(RenderSurface *surface, const Vector<2, float> &position, const Vector<2, float> &size,
                      float scale, bool alignPixels) override;
    void endSurface() override;

private:
    friend class VulkanDrawing;
    friend class VulkanRenderSurface;

    void drawQuad();
    void setProjectionMatrix(const Matrix<float> &matrix);
    void setModelViewMatrix(const Matrix<float> &matrix);
    void setPixelScaledModelViewMatrix(const Matrix<float> &matrix);
    void setTextureMatrix(const Matrix<float> &matrix);
    void setColor(const Vector<4, float> &color);
    void setTextureEnabled(bool enabled);
    void setFilter(int filter);
    void setTextureAdjustments(float opacity, float brightness, float contrast, float saturation);
    void setWidgetDeformationEnabled(bool enabled);
    void setWidgetBindMatrix(const Matrix<float> &matrix);
    void setInverseWidgetBindMatrix(const Matrix<float> &matrix);
    void setWidgetBindPos(const Vector<2, float> &position);
    void setWidgetBindSize(const Vector<2, float> &size);
    void setDeformedWidgetPos(const Vector<2, float> &position);
    void setDeformedWidgetSize(const Vector<2, float> &size);

    struct Uniforms {
        Matrix<float> projection;
        Matrix<float> modelView;
        Matrix<float> mask;
        Matrix<float> texture;
        Matrix<float> widgetBind;
        Matrix<float> inverseWidgetBind;
        Vector<2, float> widgetBindPos {0, 0};
        Vector<2, float> widgetBindSize {0, 0};
        Vector<2, float> deformedWidgetPos {0, 0};
        Vector<2, float> deformedWidgetSize {0, 0};
        Vector<4, float> color {1, 1, 1, 1};
        Vector<4, float> textureAdjustments {1, 1, 1, 1};
        uint32_t widgetDeformation = 0;
        uint32_t textureEnabled = 0;
        int32_t filter = FILTER_NONE;
        uint32_t padding = 0;
    };
    static_assert(sizeof(Uniforms) == 464);

    gfx::VulkanContext &m_context;
    Matrix<float> m_modelView;
    Matrix<float> m_projection;
    std::list<Matrix<float>> m_projectionStack;
    std::list<Matrix<float>> m_modelViewStack;
    gfx::BufferPtr m_quadVertices;
    gfx::BufferPtr m_quadIndices;
    Uniforms m_uniforms;
};

}

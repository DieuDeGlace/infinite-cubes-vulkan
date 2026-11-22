#pragma once
#include "gfx/AlphaRenderDevice.h"
#include "plasma/Engine.h"

namespace plasma {
extern int g_drawingLayer;

// Shape pixel-shader filter selector, chosen from Display clipping and blend flags.
enum D3D9FilterMode {
    D3D9_FILTER_NONE = 0,
    D3D9_FILTER_CLIP = 1,
    D3D9_FILTER_MODULATION = 2,
    D3D9_FILTER_SCREEN = 3
};

class VulkanDrawing;
class VulkanRenderSurface;

class VulkanEngine : public Engine {
public:
    explicit VulkanEngine(AlphaRenderDevice *device);
    virtual ~VulkanEngine();
    virtual bool setupShadersAndFrameBuffer();
    virtual Texture *createTexture(int width, int height, const unsigned char *pixels, const TextureFormat &format,
                                   const std::wstring &name, bool transient);
    virtual void drawCaret(const Vector<2, float> &, const Vector<2, float> &, const Vector<4, float> &);
    virtual void invertRectangle(const Vector<2, float> &, const Vector<2, float> &);
    virtual void drawTexturedRectangle(const Vector<2, float> &, const Vector<2, float> &, const Vector<2, float> &,
                                       const Vector<2, float> &, const Vector<4, float> &);
    virtual void fillRectangle(const Vector<2, float> &, const Vector<2, float> &, const Vector<4, float> &);
    virtual void pushIdentityMatrix() {}
    virtual void popMatrix() {}
    virtual void enableTexture2D() {}
    virtual void disableTexture2D() {}
    virtual Drawing *createDrawing();
    virtual RenderSurface *createRenderSurface(bool depthAttachment);
    virtual void getModelViewMatrix(Matrix<float> &matrix);
    virtual void loadMatrix(Matrix<float> &matrix);
    virtual void beginTextDrawing(Node *node);
    virtual void endTextDrawing(Node *) {}
    virtual void matrixLoaded();
    virtual void beginWindowDrawing();
    virtual void endWindowDrawing();
    virtual void pushNodeMatrix(Node *node);
    virtual void popNodeMatrix(Node *node);
    virtual void unused23(Node *) {}
    virtual void unused24(Node *) {}
    virtual void beginSurface(RenderSurface *, const Vector<2, float> &, const Vector<2, float> &, float scale,
                              bool alignPixels);
    virtual void endSurface();

    void restoreDeviceResources();
    void releaseDeviceResources();
    void drawSurfaceRectangle(const Vector<2, float> &position, const Vector<2, float> &size,
                              const Vector<2, float> &texturePosition, const Vector<2, float> &textureSize,
                              const Vector<4, float> &color);
    void setVertexShaderMatrix(unsigned registerIndex, const Matrix<float> &matrix, unsigned count);
    AlphaRenderDevice *getDevice() const { return m_device; }

private:
    friend class VulkanTexture;
    friend class VulkanDrawing;
    friend class VulkanRenderSurface;
    AlphaShader *m_shapeVertexShader;
    AlphaShader *m_shapePixelShader;
    AlphaVertexLayout *m_shapeDeclaration;
    AlphaShader *m_surfaceVertexShader;
    AlphaShader *m_surfacePixelShader;
    AlphaShader *m_downsample2PixelShader;
    AlphaShader *m_downsample3PixelShader;
    AlphaShader *m_downsample4PixelShader;
    AlphaShader *m_horizontalBlurPixelShader;
    AlphaShader *m_verticalBlurPixelShader;
    unsigned m_widgetDeformationRegister;
    unsigned m_projectionRegister;
    unsigned m_modelViewRegister;
    unsigned m_widgetBindMatrixRegister;
    unsigned m_inverseWidgetBindMatrixRegister;
    unsigned m_maskMatrixRegister;
    unsigned m_textureMatrixRegister;
    unsigned m_normalMatrixRegister;
    unsigned m_widgetBindPositionRegister;
    unsigned m_widgetBindSizeRegister;
    unsigned m_deformedWidgetPositionRegister;
    unsigned m_deformedWidgetSizeRegister;
    unsigned m_antialiasRegister;
    unsigned m_baseColorRegister;
    unsigned m_textureEnabledRegister;
    unsigned m_filterRegister;
    unsigned m_textureOpacityRegister;
    unsigned m_textureBrightnessRegister;
    unsigned m_textureContrastRegister;
    unsigned m_textureSaturationRegister;
    unsigned m_surfaceProjectionRegister;
    unsigned m_downsample2ScaleRegister;
    unsigned m_downsample3ScaleRegister;
    unsigned m_downsample4ScaleRegister;
    unsigned m_blur3ScaleRegister;
    unsigned m_blur2ScaleRegister;
    Matrix<float> m_modelViewMatrix;
    Matrix<float> m_projectionMatrix;
    float m_antialiasStrength;
    std::list<Matrix<float>> m_projectionStack;
    std::list<Matrix<float>> m_modelViewStack;
    std::list<VulkanDrawing *> m_drawings;
    AlphaRenderDevice *m_device; // borrowed
    AlphaBuffer *m_rectangleVertices;
    AlphaBuffer *m_rectangleIndices;
    AlphaSurface *m_windowRenderTarget;
};
}

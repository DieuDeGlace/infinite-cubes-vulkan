#pragma once
#include "gfx/AlphaRenderDevice.h"
#include "plasma/Vector.h"
#include "plasma/Matrix.h"

namespace cube {

// D3D9 handles and constant registers, named by the embedded CTAB records.
class CubeShader {
public:
    explicit CubeShader(AlphaRenderDevice *device);
    virtual ~CubeShader();
    void apply();
    void applyCloud();
    void applySky();
    void applyWater();
    bool initialize();
    void setAlpha(float alpha);
    void setCameraPosition(const plasma::Vector<3, float> &position);
    void setDarknessColor(const plasma::Vector<4, float> &color);
    void setFogGradientTranslation(float distance);
    void setFogScale(float distance);
    void setLight(const plasma::Vector<4, float> &front, const plasma::Vector<4, float> &back,
                  const plasma::Vector<3, float> &direction, const plasma::Vector<4, float> &ambient);
    void setMaterialColor(const plasma::Vector<4, float> &color);
    void setMatrices(const plasma::Matrix<float> &world, const plasma::Matrix<float> &view,
                     const plasma::Matrix<float> &projection);
    void setPointLights(const plasma::Vector<4, float> *positions, const plasma::Vector<3, float> *colors);
    void setShininess(float shininess);
    void setSky(const plasma::Vector<4, float> &skyGradientStartColor,
                const plasma::Vector<4, float> &skyGradientEndColor, const plasma::Vector<4, float> &fog);
    void setWhite(float white);
    // The device the shader draws with.
    AlphaRenderDevice *getDevice() { return m_device; }

private:
    AlphaShader *m_vertexShader;
    AlphaShader *m_pixelShader;
    AlphaShader *m_skyPixelShader;
    AlphaShader *m_cloudPixelShader;
    AlphaShader *m_waterPixelShader;
    AlphaShader *m_unusedPixelShader;
    AlphaRenderDevice *m_device; // borrowed
    AlphaVertexLayout *m_vertexDeclaration;
    unsigned m_worldMatrix;
    unsigned m_worldViewMatrix;
    unsigned m_worldViewProjMatrix;
    unsigned m_materialColor;
    unsigned m_darknessColor;
    unsigned m_lightDirection;
    unsigned m_lightFrontColor;
    unsigned m_lightBackColor;
    unsigned m_ambientColor;
    unsigned m_white;
    unsigned m_skyColor1;
    unsigned m_skyColor2;
    unsigned m_fogColor;
    unsigned m_shininess;
    unsigned m_alpha;
    unsigned m_fogScale;
    unsigned m_fogGradientTranslation;
    unsigned m_pointLightPositions;
    unsigned m_pointLightColors;
    unsigned m_cameraPosition;
};
// CPU source capacity; setPointLights uploads the first ten entries.
enum {
    PointLightSourceCount = 16
};

}

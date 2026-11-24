#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>
#include "gfx/Renderer.h"
#include "gfx/VulkanContext.h"
#include "gfx/VulkanWorldData.h"

namespace plasma {
class Engine;
}

namespace gfx {

class VulkanRenderer final : public Renderer {
public:
    VulkanRenderer(cube::World *world, cube::WorldMap *worldMap, plasma::Engine *engine, VulkanContext &context,
                   bool alphaLighting, bool sunShadows, bool rsmGI);

    void activateMode(int mode) override;
    void drawInterface() override;
    void drawSky() override;
    void drawGroundSpots() override;
    void drawShadows(const plasma::Vector<4, float> &lightColor) override;
    void drawTrails() override;
    void beginWorldEffects(float sunlight, const plasma::Vector<3, float> &cameraPosition, bool allowTemporal) override;
    void endWorldEffects() override;
    void beginWater(bool submerged) override;
    void drawQuestMarkers(const std::vector<QuestMarker> &markers, const plasma::Matrix<float> &view,
                          const plasma::Matrix<float> &projection, bool overlay = false) override;
    void setGlobalIllumination(bool enabled) override;
    void setSunShadows(bool enabled) override;
    void setMovingLeaves(bool enabled) override;
    void drawSunMap(const plasma::Vector<3, float> &direction, const plasma::Vector<3, cube::Fixed> &focus,
                    float strength, bool reflective) override;

    void setWorldMatrix(const plasma::Matrix<float> &matrix) override;
    void setViewMatrix(const plasma::Matrix<float> &matrix) override;
    void setProjectionMatrix(const plasma::Matrix<float> &matrix) override;
    void setMatrices(const plasma::Matrix<float> &world, const plasma::Matrix<float> &view,
                     const plasma::Matrix<float> &projection) override;
    void setAlpha(float value) override;
    void setGlow(float value) override;
    void setFogScale(float value) override;
    void setFogGradientTranslation(float value) override;
    void setDepthOffset(float value) override;
    void setMaterialColor(const plasma::Vector<4, float> &value) override;
    void setSky(const plasma::Vector<4, float> &skyColor1, const plasma::Vector<4, float> &skyColor2,
                const plasma::Vector<4, float> &fogColor, const plasma::Vector<4, float> &sunColor) override;
    void setSunScreenPosition(const plasma::Vector<3, float> &direction, const plasma::Matrix<float> &view,
                              const plasma::Matrix<float> &projection, const float &cameraPitch, int width,
                              int height) override;
    void setShininess(float value) override;
    void setWhite(float value) override;
    void setLight(const plasma::Vector<4, float> &frontColor, const plasma::Vector<4, float> &backColor,
                  const plasma::Vector<3, float> &direction, const plasma::Vector<4, float> &ambientColor) override;
    void setAmbientColor(const plasma::Vector<4, float> &color) override;
    void setPointLights(const std::array<plasma::Vector<4, float>, 16> &positions,
                        const std::array<plasma::Vector<4, float>, 16> &colors) override;
    bool drawSpriteInstances(cube::Sprite *sprite, const SpriteBatch::Instance *const *instances,
                             size_t count) override;

private:
    void drawDynamic(const std::vector<TexturedVertex> &vertices, const std::vector<uint32_t> &indices);
    void beginEffects(bool textured);

    VulkanWorldUniforms m_uniforms;
    std::vector<VulkanSpriteInstance> m_spriteInstances; // drawSpriteInstances' records, kept to reuse their storage
    plasma::Engine *m_engine;
    VulkanContext &m_context;
    BufferPtr m_quadVertices;
    BufferPtr m_quadIndices;
    ImagePtr m_spotTexture;
    ImagePtr m_ringTexture;
    ImagePtr m_questIcons;
    bool m_sunShadows;
    bool m_rsmGI;
    bool m_movingLeaves = true;
    ImagePtr m_sunMap;
    ImagePtr m_reflectance, m_reflectiveDepth;
    std::optional<plasma::Vector<3, cube::Fixed>> m_shadowAnchor;
    std::array<double, 2> m_shadowPhase = {};
    TemporalFrame m_temporalFrame;
    std::array<double, 16> m_previousClip = {};
    std::array<float, 2> m_previousJitter = {};
    std::array<int64_t, 2> m_previousOrigin = {};
    plasma::Vector<3, cube::Fixed> m_previousEye;
    unsigned m_jitterIndex = 0;
    bool m_previousViewValid = false;
};

}

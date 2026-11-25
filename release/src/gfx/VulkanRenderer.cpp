#include "gfx/VulkanRenderer.h"
#define FREEIMAGE_LIB
#include "VulkanQuestIcons.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <memory>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "cube/World.h"
#include "cube/MathConstants.h"
#include "cube/Sprite.h"
#include "cube/SpriteManager.h"
#include "gfx/AlphaLighting.h"
#include "gfx/Graphics.h"
#include "gfx/IndexBuffer.h"
#include "gfx/VertexBuffer.h"
#include "plasma/Engine.h"
#include "plasma/EditFilter.h"

namespace gfx {

static_assert(sizeof(TexturedVertex) == 28 && offsetof(TexturedVertex, color) == 16 &&
              offsetof(TexturedVertex, textureCoordinate) == 20);

static const float RingTextureWaveColorAmplitude = 30.0f;
static const float RingTextureMaximumAlpha = 200.0f;
static const float GroundSpotSurfaceLift = 0.0035f;
static const int GroundShadowMaximumOpacity = 100;
static const float TrailOuterMaximumOpacity = 50.0f;
static const float TrailInnerMaximumOpacity = 200.0f;

VulkanRenderer::VulkanRenderer(cube::World *world, cube::WorldMap *worldMap, plasma::Engine *engine,
                               VulkanContext &context, bool alphaLighting, bool sunShadows, bool rsmGI)
    : Renderer(world, worldMap, alphaLighting),
      m_engine(engine),
      m_context(context),
      m_sunShadows(sunShadows),
      m_rsmGI(rsmGI)
{
    m_uniforms.projection.identity();
    m_uniforms.view.identity();
    m_uniforms.world.identity();
    const plasma::Vector<4, float> zero(0, 0, 0, 0);
    m_uniforms.lightPositions.fill(zero);
    m_uniforms.lightColors.fill(zero);
    m_uniforms.skyColors.fill(zero);
    m_uniforms.shadowTransform.fill(zero);

    Vertex vertices[4] = {
        Vertex(plasma::Vector<3, int>(0, 0, 0), plasma::Vector<3, int>(0, 0, 1),
               plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f)),
        Vertex(plasma::Vector<3, int>(1, 0, 0), plasma::Vector<3, int>(0, 0, 1),
               plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f)),
        Vertex(plasma::Vector<3, int>(1, 1, 0), plasma::Vector<3, int>(0, 0, 1),
               plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f)),
        Vertex(plasma::Vector<3, int>(0, 1, 0), plasma::Vector<3, int>(0, 0, 1),
               plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f)),
    };
    const uint32_t indices[] = {0, 1, 2, 0, 2, 3};
    m_quadVertices = m_context.createBuffer(vertices, sizeof(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    m_quadIndices = m_context.createBuffer(indices, sizeof(indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    std::vector<plasma::Vector<4, unsigned char>> texels(256 * 256);

    // A soft round spot: white, with alpha falling from the centre.
    plasma::Vector<4, unsigned char> texel;
    for (int y = 0; y < 256; y++) {
        for (int x = 0; x < 256; x++) {
            float dx = (float)x - 127.5f;
            float dy = (float)y - 127.5f;
            float value = std::max(0.0f, 1.0f - (dx * dx + dy * dy) / (128.0f * 128.0f));
            texel[0] = 255;
            texel[1] = 255;
            texel[2] = 255;
            value *= value * value;
            texel[3] = (unsigned char)(value * 255.0f);
            texels[y * 256 + x] = texel;
        }
    }
    m_spotTexture = m_context.createTexture(256, 256, texels.data());

    // Concentric rings that fade out towards the edge.
    for (int y = 0; y < 256; y++) {
        for (int x = 0; x < 256; x++) {
            float dx = (float)x - 127.5f;
            float dy = (float)y - 127.5f;
            float radius = std::min(1.0f, sqrtf(dx * dx + dy * dy) / 128.0f);
            float wave = cosf((float)(radius * 10.0f * cube::Pi)) * RingTextureWaveColorAmplitude;
            float fade = 1.0f - radius * radius * radius;
            // Ring texture RGB palette: (220,190,200), with half wave amplitude in green and blue.
            texel[0] = (unsigned char)(wave + 220.0f);
            texel[1] = (unsigned char)(wave * 0.5f + 190.0f);
            texel[2] = (unsigned char)(wave * 0.5f + 200.0f);
            texel[3] = (unsigned char)(fade * RingTextureMaximumAlpha);
            texels[y * 256 + x] = texel;
        }
    }
    m_ringTexture = m_context.createTexture(256, 256, texels.data());
    m_questIcons = loadQuestIcons(m_context);
}

void VulkanRenderer::beginWorldEffects(float sunlight, const plasma::Vector<3, float> &cameraPosition,
                                       bool allowTemporal)
{
    m_uniforms.effects = {m_alphaLighting ? 1.0f : 0.0f, sunlight, 0, 0};
    m_uniforms.sceneCamera = {cameraPosition[0], cameraPosition[1], cameraPosition[2], 1};
    m_uniforms.foliageWind = {0, 0, 0, 0};
    if (m_movingLeaves) {
        // The shaders add the phase gradients (WorldUniforms.hlsli) times the rebased position, so the wind stays put
        // in the world when the renderer rebases to the player. A slow gust varies the reach.
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        const double x = -double(m_drawOriginX), y = -double(m_drawOriginY);
        const double turn = 2.0 * cube::Pi;
        const float sway = float(std::remainder(0.21 * x + 0.17 * y + 1.3 * seconds, turn));
        const float flutter = float(std::remainder(-0.53 * x + 0.61 * y + 3.1 * seconds, turn));
        const float gust = float(0.75 + 0.25 * std::sin(std::remainder(0.37 * seconds, turn)));
        m_uniforms.foliageWind = {sway, flutter, 0.09f * gust, 0};
    }
    m_uniforms.temporalJitter = {0, 0, 0, 0};
    m_temporalFrame = {};
    if (!allowTemporal || !m_context.temporalEnabled()) {
        m_previousViewValid = false;
        return;
    }

    const std::array<int64_t, 2> origin = {-m_drawOriginX.raw(), -m_drawOriginY.raw()};
    const auto view = glm::dmat4(glm::make_mat4(&m_viewMatrix[0]));
    const auto projection = glm::dmat4(glm::make_mat4(&m_projectionMatrix[0]));
    const auto clip = projection * view;
    float pixelX = 0, pixelY = 0;
    const bool stable =
        m_previousViewValid && plasma::Vector<3, float>(m_cameraPosition - m_previousEye).squaredLength() < 64.0f;
    if (stable) {
        // Eight centered Halton samples of bases 2 and 3, in pixel units.
        static constexpr std::array<std::array<float, 2>, 8> Halton = {{{{0.5f, 1.0f / 3}},
                                                                        {{0.25f, 2.0f / 3}},
                                                                        {{0.75f, 1.0f / 9}},
                                                                        {{0.125f, 4.0f / 9}},
                                                                        {{0.625f, 7.0f / 9}},
                                                                        {{0.375f, 2.0f / 9}},
                                                                        {{0.875f, 5.0f / 9}},
                                                                        {{0.0625f, 8.0f / 9}}}};
        const auto &sample = Halton[m_jitterIndex++ % Halton.size()];
        pixelX = (sample[0] - 0.5f) * 0.6f;
        pixelY = (sample[1] - 0.5f) * 0.6f;
        const float clipX = 2 * pixelX / float(m_width);
        const float clipY = -2 * pixelY / float(m_height);
        m_uniforms.temporalJitter = {clipX, clipY, 0, 0};
        // History lives on the unjittered grid. World draws are rebased to the player.
        const glm::dvec3 originDelta(double(origin[0] - m_previousOrigin[0]) / cube::FixedUnitsPerBlock,
                                     double(origin[1] - m_previousOrigin[1]) / cube::FixedUnitsPerBlock, 0);
        const auto previous = glm::make_mat4(m_previousClip.data());
        const auto clipToPrevious =
            glm::mat4(previous * glm::translate(glm::dmat4(1), originDelta) * glm::inverse(clip));
        std::copy_n(glm::value_ptr(clipToPrevious), 16, m_temporalFrame.clipToPrevious.begin());
        m_temporalFrame.jitterPixels = {pixelX, pixelY, m_previousJitter[0], m_previousJitter[1]};
        m_temporalFrame.reproject = true;
    }
    std::copy_n(glm::value_ptr(clip), 16, m_previousClip.begin());
    m_previousJitter = {pixelX, pixelY};
    m_previousOrigin = origin;
    m_previousEye = m_cameraPosition;
    m_previousViewValid = true;
}

void VulkanRenderer::endWorldEffects()
{
    m_uniforms.effects = {0, 1, 0, 0};
    m_uniforms.temporalJitter = {0, 0, 0, 0};
    m_uniforms.shadowProjection[3] = 0;
    m_context.bindTexture(1, {});
    m_context.bindTexture(2, {});
    m_context.bindTexture(3, {});
    m_context.finishScene(m_temporalFrame);
}

void VulkanRenderer::setGlobalIllumination(bool enabled)
{
    if (m_rsmGI != enabled) {
        m_rsmGI = enabled;
        m_previousViewValid = false;
    }
}

void VulkanRenderer::setMovingLeaves(bool enabled)
{
    m_movingLeaves = enabled;
}

void VulkanRenderer::setSunShadows(bool enabled)
{
    if (m_sunShadows == enabled)
        return;
    m_sunShadows = enabled;
    m_previousViewValid = false;
    if (!enabled)
        m_sunMap.reset();
}

void VulkanRenderer::beginWater(bool submerged)
{
    m_context.snapshotWaterBackground();
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const double x = -double(m_drawOriginX), y = -double(m_drawOriginY);
    const double turn = 2.0 * cube::Pi;
    // Keep the ripples anchored to the world when the renderer rebases to the player.
    const float phaseX = float(std::remainder(0.65 * x + 0.31 * y + 0.8 * seconds, turn));
    const float phaseY = float(std::remainder(0.72 * y - 0.27 * x - 0.65 * seconds, turn));
    const float brightness = 0.2f + 0.8f * alpha::sunlight(m_world->m_state.m_time);
    m_uniforms.waterSurface = {phaseX, phaseY, brightness, submerged ? 1.0f : 0.0f};
}

void VulkanRenderer::drawSunMap(const plasma::Vector<3, float> &direction, const plasma::Vector<3, cube::Fixed> &focus,
                                float strength, bool reflective)
{
    const bool rsmGI = m_rsmGI && reflective;
    if ((!m_sunShadows && !rsmGI) || direction[2] <= 0.08f || strength <= 0.0f || m_zones.empty())
        return;
#ifdef __ANDROID__
    constexpr uint32_t Size = 1536;
    constexpr float Radius = 56.0f;
#else
    constexpr uint32_t Size = 3072;
    constexpr float Radius = 70.0f;
#endif
    constexpr uint32_t ReflectiveSize = 512;
    constexpr float DepthRange = 384.0f;
    static_assert(Size % ReflectiveSize == 0);
    const double texelSize = 2.0 * Radius / (rsmGI ? ReflectiveSize : Size);
    // Keep the rotating texel grid near the player so the sun never rotates it around a distant world origin.
    if (!m_shadowAnchor)
        m_shadowAnchor = focus;
    const plasma::Vector<3, double> offset(focus - *m_shadowAnchor);
    const bool moveAnchor = offset.squaredLength() > 4.0 * Radius * Radius;
    // A fixed horizontal reference keeps the map basis continuous through an overhead sun.
    const plasma::Vector<3, float> reference(0, 1, 0);
    auto right = reference.cross(direction);
    right.normalize();
    const auto up = direction.cross(right);
    const auto axis = [&](const plasma::Vector<3, float> &v, double &phase) {
        const double snap = std::remainder(offset.dot(plasma::Vector<3, double>(v)) + phase, texelSize);
        if (moveAnchor)
            phase = snap;
        return plasma::Vector<4, float>(v[0] / Radius, v[1] / Radius, v[2] / Radius,
                                        float((snap - double(focus[2]) * v[2]) / Radius));
    };
    m_uniforms.shadowTransform[0] = axis(right, m_shadowPhase[0]);
    m_uniforms.shadowTransform[1] = axis(up, m_shadowPhase[1]);
    if (moveAnchor)
        m_shadowAnchor = focus;
    m_uniforms.shadowTransform[2] = {-direction[0] / DepthRange, -direction[1] / DepthRange, -direction[2] / DepthRange,
                                     0.5f + float(focus[2]) * direction[2] / DepthRange};
    const int zoneX = cube::floorDivide(focus[0].toBlock(), cube::BlocksPerZone);
    const int zoneY = cube::floorDivide(focus[1].toBlock(), cube::BlocksPerZone);
    constexpr int Reach = int(Radius / cube::BlocksPerZone) + 2;
    auto *graphics = m_world->m_spriteManager->graphics();
    const auto drawChunks = [&] {
        for (int x = zoneX - Reach; x <= zoneX + Reach; ++x) {
            for (int y = zoneY - Reach; y <= zoneY + Reach; ++y) {
                const auto *zone = m_world->zone(x, y);
                if (!zone)
                    continue;
                for (const auto *buffer : zone->m_chunk.chunkBuffers) {
                    auto *indices = buffer->indexBuffers[ChunkPassSolid];
                    if (!indices || !buffer->vertexBuffer->bind() || !indices->bind())
                        continue;
                    plasma::Matrix<float> model;
                    model.identity();
                    model.translate(float(cube::Fixed(x * cube::BlocksPerZone) + m_drawOriginX),
                                    float(cube::Fixed(y * cube::BlocksPerZone) + m_drawOriginY), float(buffer->baseZ));
                    setWorldMatrix(model);
                    graphics->drawIndexed(buffer->vertexCount, buffer->triangleCounts[ChunkPassSolid]);
                }
            }
        }
    };
    // Whether any part of the instance projects into the map. Plants are collected out to the full effect distance,
    // mostly beyond the map's radius, where their vertices would be transformed only to be clipped.
    const auto reachesMap = [&](const cube::Sprite *sprite, const SpriteBatch::Instance &instance) {
        const plasma::Matrix<float> &matrix = instance.matrix;
        const plasma::Vector<3, float> half(sprite->m_size[0] * 0.5f, sprite->m_size[1] * 0.5f,
                                            sprite->m_size[2] * 0.5f);
        const plasma::Vector<3, float> center = matrix * half;
        // Each axis of the box moves a corner from the centre by at most its column's length times the half size;
        // one block more covers the sway and the leaves' flutter.
        float reach = 1.0f;
        for (int axis = 0; axis < 3; ++axis) {
            const float *column = &matrix[axis * 4];
            reach += std::sqrt(column[0] * column[0] + column[1] * column[1] + column[2] * column[2]) * half[axis];
        }
        // The first two rows of the shadow transform give the map's coordinates (-1 to 1) per block over Radius.
        for (int row = 0; row < 2; ++row) {
            const auto &axis = m_uniforms.shadowTransform[row];
            const float coordinate = axis[0] * center[0] + axis[1] * center[1] + axis[2] * center[2] + axis[3];
            if (std::abs(coordinate) > 1.0f + reach / Radius)
                return false;
        }
        return true;
    };
    // Each model's instances in the map cast their shadows in one instanced draw. Plants, the models that have a far
    // copy (cube::Sprite::lowDetail), cast with its level 1: a joined voxel is about two shadow texels wide, which the
    // filtered shadow edge hides, for a quarter of the faces.
    std::vector<const SpriteBatch::Instance *> instances;
    std::vector<SpriteBatch::Instance> joined;
    const auto drawModels = [&](const auto &groups) {
        for (const auto &group : groups) {
            auto *sprite = group.first;
            cube::Sprite *caster = sprite->m_lowDetail ? sprite->m_lowDetail : sprite;
            instances.clear();
            joined.clear();
            for (const auto &instance : group.second) {
                if (!reachesMap(sprite, instance))
                    continue;
                if (caster == sprite) {
                    instances.push_back(&instance);
                    continue;
                }
                // The copy's voxels are twice the size, from the same corner.
                joined.push_back(instance);
                for (int element = 0; element < 12; ++element)
                    joined.back().matrix[element] *= 2.0f;
            }
            for (const auto &instance : joined)
                instances.push_back(&instance);
            if (!instances.empty() && caster->bind()) {
                drawSpriteInstances(caster, instances.data(), instances.size());
                m_renderStats.shadowCasters += (int)instances.size();
            }
        }
    };
    if (m_sunShadows) {
        if (!m_sunMap)
            m_sunMap = m_context.createSampledDepth({Size, Size});
        m_context.beginSurface({}, m_sunMap);
        m_context.setDrawState({VulkanProgram::Shadow, RasterizerTwoSided, DepthTestWrite, BlendDepthOnly}, &m_uniforms,
                               sizeof(m_uniforms));
        drawChunks();
        drawModels(m_worldBatch.m_instances);
        drawModels(m_worldBatch.m_mirroredInstances);
        m_context.endSurface();
        const float daylight = std::clamp((direction[2] - 0.08f) / 0.25f, 0.0f, 1.0f);
        m_uniforms.effects[2] = strength * daylight * daylight * (3.0f - 2.0f * daylight);
        m_uniforms.effects[3] = 0.045f / DepthRange;
        m_context.bindTexture(2, m_sunMap);
    }
    if (rsmGI) {
        if (!m_reflectance) {
            m_reflectance = m_context.createSurface(ReflectiveSize, ReflectiveSize, false);
            m_reflectiveDepth = m_context.createSampledDepth({ReflectiveSize, ReflectiveSize});
        }
        m_context.beginSurface(m_reflectance, m_reflectiveDepth);
        m_context.setDrawState({VulkanProgram::ReflectiveShadow, RasterizerTwoSided, DepthTestWrite, BlendOpaque},
                               &m_uniforms, sizeof(m_uniforms));
        drawChunks();
        m_context.endSurface();
        m_context.bindTexture(1, m_reflectance);
        m_context.bindTexture(3, m_reflectiveDepth);
    }
    m_uniforms.shadowProjection = {1.0f / Size, Radius, DepthRange, rsmGI ? 1.0f / ReflectiveSize : 0.0f};
}

void VulkanRenderer::activateMode(int mode)
{
    static constexpr VulkanDrawState modes[] = {
        {VulkanProgram::World, RasterizerFront, DepthTestWrite, BlendOpaque},
        {VulkanProgram::World, RasterizerBack, DepthTestWrite, BlendOpaque},
        {VulkanProgram::World, RasterizerTwoSided, DepthTestWrite, BlendOpaque},
        {VulkanProgram::World, RasterizerFront, DepthTestWrite, BlendDepthOnly},
        {VulkanProgram::World, RasterizerBack, DepthTestWrite, BlendDepthOnly},
        {VulkanProgram::World, RasterizerTwoSided, DepthTestWrite, BlendDepthOnly},
        {VulkanProgram::World, RasterizerFront, DepthTestWrite, BlendAlpha},
        {VulkanProgram::World, RasterizerBack, DepthTestWrite, BlendAlpha},
        {VulkanProgram::Sky, RasterizerTwoSided, DepthDisabled, BlendOpaque},
        {VulkanProgram::Water, RasterizerTwoSided, DepthTestWrite, BlendOpaque},
        {VulkanProgram::Clouds, RasterizerFront, DepthTestWrite, BlendAlpha},
    };
    if (mode >= RendererMode_OpaqueFront && mode <= RendererMode_Clouds)
        m_context.setDrawState(modes[mode], &m_uniforms, sizeof(m_uniforms));
}

void VulkanRenderer::beginEffects(bool textured)
{
    m_uniforms.textureEnabled = textured;
    m_context.setDrawState({VulkanProgram::Textured, RasterizerTwoSided, DepthTestNoWrite, BlendAlpha}, &m_uniforms,
                           sizeof(m_uniforms));
}

void VulkanRenderer::drawDynamic(const std::vector<TexturedVertex> &vertices, const std::vector<uint32_t> &indices)
{
    m_context.drawTransient(vertices.data(), sizeof(TexturedVertex) * vertices.size(), indices.data(),
                            static_cast<uint32_t>(indices.size()));
}

void VulkanRenderer::drawQuestMarkers(const std::vector<QuestMarker> &markers, const plasma::Matrix<float> &view,
                                      const plasma::Matrix<float> &projection, bool overlay)
{
    if (markers.empty() || !m_questIcons)
        return;

    const auto clipFromWorld = glm::make_mat4(&projection[0]) * glm::make_mat4(&view[0]);
    std::vector<TexturedVertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(markers.size() * 4);
    indices.reserve(markers.size() * 6);
    for (const auto &marker : markers) {
        const auto &position = marker.position;
        const auto clip = clipFromWorld * glm::vec4(position[0], position[1], position[2], 1.0f);
        if (clip.w <= 0.0f || clip.z < 0.0f || clip.z >= clip.w)
            continue;

        appendQuestIcon(marker.icon, {clip.x, clip.y, clip.z, clip.w}, m_width, m_height, vertices, indices);
    }
    if (indices.empty())
        return;

    const auto worldMatrix = m_uniforms.world;
    const auto viewMatrix = m_uniforms.view;
    const auto projectionMatrix = m_uniforms.projection;
    const auto textureEnabled = m_uniforms.textureEnabled;
    m_uniforms.world.identity();
    m_uniforms.view.identity();
    m_uniforms.projection.identity();
    beginEffects(true);
    if (overlay)
        m_context.setDrawState({VulkanProgram::Textured, RasterizerTwoSided, DepthDisabled, BlendAlpha}, &m_uniforms,
                               sizeof(m_uniforms));
    m_context.bindTexture(0, m_questIcons);
    drawDynamic(vertices, indices);
    m_context.bindTexture(0, {});
    m_uniforms.world = worldMatrix;
    m_uniforms.view = viewMatrix;
    m_uniforms.projection = projectionMatrix;
    m_uniforms.textureEnabled = textureEnabled;
}

void VulkanRenderer::drawGroundSpots()
{
    beginEffects(true);
    for (GroundSpot &spot : m_groundSpots) {
        plasma::Vector<3, int> block = cube::fixedToBlock(spot.position);
        float radius = spot.radius;
        float scale = 1.0f / (radius + radius);
        m_context.bindTexture(0, spot.kind == 0 ? m_spotTexture : m_ringTexture);
        int extent = (int)(radius + 1.0f);
        plasma::Vector<4, unsigned char> color((int)(spot.color[0] * 255.0f), (int)(spot.color[1] * 255.0f),
                                               (int)(spot.color[2] * 255.0f), (int)(spot.color[3] * 255.0f));
        for (int dx = -extent; dx <= extent; dx++) {
            for (int dy = -extent; dy <= extent; dy++) {
                // The quad lies on the first block below the spot that is neither air nor water; none within 100
                // blocks, or a spot inside the ground, draws nothing.
                int x = dx + block[0];
                int y = dy + block[1];
                int z = block[2];
                int steps = 0;
                cube::Block below = m_world->getBlock(x, y, z);
                if (below.isSolid())
                    continue;
                while (!below.isSolid()) {
                    z--;
                    below = m_world->getBlock(x, y, z);
                    steps++;
                    if (steps > 100)
                        break;
                }
                if (steps > 100)
                    continue;
                z++;
                float u = ((float)dx + radius - (float)(spot.position[0] - cube::Fixed(block[0]))) * scale;
                float v = ((float)dy + radius - (float)(spot.position[1] - cube::Fixed(block[1]))) * scale;
                plasma::Matrix<float> world;
                world.identity();
                world.translate((float)(cube::Fixed(block[0]) + m_drawOriginX),
                                (float)(cube::Fixed(block[1]) + m_drawOriginY), (float)z + GroundSpotSurfaceLift);
                setWorldMatrix(world);
                std::vector<uint32_t> indices = {0, 1, 2, 0, 3, 2};
                TexturedVertex vertexData[4] = {
                    {plasma::Vector<4, float>((float)dx, (float)dy, 0.0f, 1.0f), color, plasma::Vector<2, float>(u, v)},
                    {plasma::Vector<4, float>((float)(dx + 1), (float)dy, 0.0f, 1.0f), color,
                     plasma::Vector<2, float>(u + scale, v)},
                    {plasma::Vector<4, float>((float)(dx + 1), (float)(dy + 1), 0.0f, 1.0f), color,
                     plasma::Vector<2, float>(u + scale, v + scale)},
                    {plasma::Vector<4, float>((float)dx, (float)(dy + 1), 0.0f, 1.0f), color,
                     plasma::Vector<2, float>(u, v + scale)},
                };
                std::vector<TexturedVertex> vertices(vertexData, vertexData + 4);
                drawDynamic(vertices, indices);
            }
        }
    }
}

void VulkanRenderer::drawInterface()
{
    m_engine->drawWindow(0);
}

void VulkanRenderer::drawShadows(const plasma::Vector<4, float> &lightColor)
{
    beginEffects(false);
    plasma::Matrix<float> world;
    world.identity();
    setWorldMatrix(world);
    float lift = 0.0f;
    for (GroundShadow &shadow : m_shadows) {
        // The shadow points away from the sun's compass direction.
        float angle = (float)(m_cameraAngle[2] / 180.0f * cube::Pi);
        float sine = std::sin(angle);
        float cosine = std::cos(angle);
        // Shadow opacity is multiplied by the C runtime's daylight-saving flag (_daylight). In a time zone without
        // daylight saving, shadows are invisible.
        int alpha =
            (int)(attenuation(shadow.position, m_effectViewDistance) * (float)(GroundShadowMaximumOpacity * _daylight));
        std::vector<uint32_t> indices = {0, 1, 2, 2, 1, 3};

        // The near edge, size wide, at the shadow's position, and the far edge, transparent, height lower and size *
        // height / 4 further.
        TexturedVertex vertexData[4] = {
            {plasma::Vector<4, float>((float)(m_drawOriginX + shadow.position[0] - cosine * 0.5f * shadow.size),
                                      (float)(shadow.position[1] + m_drawOriginY - sine * 0.5f * shadow.size),
                                      (float)shadow.position[2], 1.0f),
             plasma::Vector<4, unsigned char>(255, 255, 255, alpha), plasma::Vector<2, float>(0.0f, 0.0f)},
            {plasma::Vector<4, float>((float)(m_drawOriginX + shadow.position[0] + cosine * 0.5f * shadow.size),
                                      (float)(shadow.position[1] + m_drawOriginY + sine * 0.5f * shadow.size),
                                      (float)shadow.position[2], 1.0f),
             plasma::Vector<4, unsigned char>(255, 255, 255, alpha), plasma::Vector<2, float>(0.0f, 0.0f)},
            {plasma::Vector<4, float>(
                 (float)(m_drawOriginX + shadow.position[0] - cosine * shadow.size * 0.25f * shadow.height +
                         cube::Fixed((long long)lift)),
                 (float)(shadow.position[1] + m_drawOriginY - sine * shadow.size * 0.25f * shadow.height +
                         cube::Fixed((long long)lift)),
                 (float)(shadow.position[2] - shadow.height), 1.0f),
             plasma::Vector<4, unsigned char>(255, 255, 255, 0), plasma::Vector<2, float>(0.0f, 0.0f)},
            {plasma::Vector<4, float>(
                 (float)(m_drawOriginX + shadow.position[0] + cosine * shadow.size * 0.25f * shadow.height +
                         cube::Fixed((long long)lift)),
                 (float)(shadow.position[1] + m_drawOriginY + sine * shadow.size * 0.25f * shadow.height +
                         cube::Fixed((long long)lift)),
                 (float)(shadow.position[2] - shadow.height), 1.0f),
             plasma::Vector<4, unsigned char>(255, 255, 255, 0), plasma::Vector<2, float>(0.0f, 0.0f)},
        };
        std::vector<TexturedVertex> vertices(vertexData, vertexData + 4);
        drawDynamic(vertices, indices);
    }
}

void VulkanRenderer::drawSky()
{
    const auto jitter = m_uniforms.temporalJitter;
    m_uniforms.temporalJitter = {0, 0, 0, 0};
    activateMode(RendererMode_Sky);
    m_uniforms.lightPositions.fill(plasma::Vector<4, float>(0, 0, 0, 0));
    setShininess(0.0f);
    setAlpha(1.0f);

    // The unit quad scaled to (-1, -1)..(1, 1) at depth 100, seen through an orthographic projection of the square
    // (-0.5, -0.5)..(0.5, 0.5) with depth 0.1..10000.
    plasma::Matrix<float> projection;
    projection.identity();
    projection[0] = 2.0f / (0.5f - -0.5f);
    projection[5] = 2.0f / (0.5f - -0.5f);
    projection[10] = 1.0f / (10000.0f - 0.1f);
    projection[11] = -0.1f / (10000.0f - 0.1f);
    plasma::Matrix<float> view;
    view.identity();
    plasma::Matrix<float> world;
    world.identity();
    world.translate(-1.0f, -1.0f, 100.0f);
    world.scale(2.0f, 2.0f, 2.0f);
    setMatrices(world, view, projection);

    m_context.bindVertex(m_quadVertices);
    m_context.bindIndex(m_quadIndices, VK_INDEX_TYPE_UINT32);
    m_context.drawIndexed(6);
    m_uniforms.temporalJitter = jitter;
}

void VulkanRenderer::drawTrails()
{
    beginEffects(false);
    plasma::Matrix<float> world;
    world.identity();
    setWorldMatrix(world);

    // A ribbon of fifteen quads between two trails of sixteen points.
    std::vector<TexturedVertex> vertices(32);
    std::vector<uint32_t> indices(90);
    for (int i = 0; i < 15; i++) {
        indices[i * 6 + 0] = i * 2;
        indices[i * 6 + 1] = i * 2 + 1;
        indices[i * 6 + 2] = i * 2 + 2;
        indices[i * 6 + 3] = i * 2 + 1;
        indices[i * 6 + 4] = i * 2 + 2;
        indices[i * 6 + 5] = i * 2 + 3;
    }
    float lift = 0.0f;
    for (cube::Creature *creature : m_trailCreatures) {
        // The creature's drawn position relative to the draw origin, in blocks. Each trail point is copied and moved
        // by it in place.
        plasma::Vector<2, float> offset((float)(-creature->m_drawnPosition[0] - m_drawOriginX),
                                        (float)(-creature->m_drawnPosition[1] - m_drawOriginY));
        plasma::Vector<3, unsigned char> outerColor;
        plasma::Vector<3, unsigned char> innerColor;
        innerColor = plasma::Vector<3, unsigned char>(255, 255, 255);
        outerColor = plasma::Vector<3, unsigned char>(255, 255, 255);
        if (!(creature->m_animationState.m_trails[0][0] == creature->m_animationState.m_trails[0][15])) {
            for (int i = 0; i < 16; i++) {
                float t = (1.0f - (float)i / 15.0f - 0.5f) * 2.0f;
                float fade = (1.0f - t * t) * (1.0f - t * t);
                plasma::Vector<3, float> outerPoint = creature->m_animationState.m_trails[1][i];
                outerPoint[0] -= offset[0];
                outerPoint[1] -= offset[1];
                outerPoint[2] += lift;
                plasma::Vector<3, float> innerPoint = creature->m_animationState.m_trails[0][i];
                innerPoint[0] -= offset[0];
                innerPoint[1] -= offset[1];
                innerPoint[2] += lift;
                vertices[2 * i].position = plasma::Vector<4, float>(outerPoint[0], outerPoint[1], outerPoint[2], 1.0f);
                vertices[2 * i].color = plasma::Vector<4, unsigned char>(outerColor[0], outerColor[1], outerColor[2],
                                                                         (int)(fade * TrailOuterMaximumOpacity));
                vertices[2 * i + 1].position =
                    plasma::Vector<4, float>(innerPoint[0], innerPoint[1], innerPoint[2], 1.0f);
                vertices[2 * i + 1].color = plasma::Vector<4, unsigned char>(
                    innerColor[0], innerColor[1], innerColor[2], (int)(fade * TrailInnerMaximumOpacity));
            }
            drawDynamic(vertices, indices);
        }
        if (!(creature->m_animationState.m_trails[2][0] == creature->m_animationState.m_trails[2][15])) {
            for (int i = 0; i < 16; i++) {
                float t = (1.0f - (float)i / 15.0f - 0.5f) * 2.0f;
                float fade = (1.0f - t * t) * (1.0f - t * t);
                plasma::Vector<3, float> outerPoint = creature->m_animationState.m_trails[3][i];
                outerPoint[0] -= offset[0];
                outerPoint[1] -= offset[1];
                outerPoint[2] += lift;
                plasma::Vector<3, float> innerPoint = creature->m_animationState.m_trails[2][i];
                innerPoint[0] -= offset[0];
                innerPoint[1] -= offset[1];
                innerPoint[2] += lift;
                vertices[2 * i].position = plasma::Vector<4, float>(outerPoint[0], outerPoint[1], outerPoint[2], 1.0f);
                vertices[2 * i].color = plasma::Vector<4, unsigned char>(outerColor[0], outerColor[1], outerColor[2],
                                                                         (int)(fade * TrailOuterMaximumOpacity));
                vertices[2 * i + 1].position =
                    plasma::Vector<4, float>(innerPoint[0], innerPoint[1], innerPoint[2], 1.0f);
                vertices[2 * i + 1].color = plasma::Vector<4, unsigned char>(
                    innerColor[0], innerColor[1], innerColor[2], (int)(fade * TrailInnerMaximumOpacity));
            }
            drawDynamic(vertices, indices);
        }
    }
}

void VulkanRenderer::setAlpha(float value)
{
    m_uniforms.alpha = value;
}

void VulkanRenderer::setAmbientColor(const plasma::Vector<4, float> &color)
{
    m_uniforms.ambient = color;
}

void VulkanRenderer::setDepthOffset(float value)
{
    m_uniforms.depthOffset = value;
}

void VulkanRenderer::setFogGradientTranslation(float value)
{
    m_uniforms.fogGradient = (value - 80.0f) / -30.0f;
}

void VulkanRenderer::setFogScale(float value)
{
    if (value > 0.0f)
        m_uniforms.fogScale = 1.0f / value;
}

void VulkanRenderer::setGlow(float value)
{
    m_uniforms.glow = value;
}

void VulkanRenderer::setLight(const plasma::Vector<4, float> &frontColor, const plasma::Vector<4, float> &backColor,
                              const plasma::Vector<3, float> &direction, const plasma::Vector<4, float> &ambientColor)
{
    const float length =
        std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
    const auto normal = length > 0.0f ? direction * (1.0f / length) : plasma::Vector<3, float>(0, 0, 1);
    m_uniforms.frontColor = frontColor;
    m_uniforms.backColor = backColor;
    m_uniforms.lightDirection = plasma::Vector<4, float>(normal[0], normal[1], normal[2], 1.0f);
    setAmbientColor(ambientColor);
}

void VulkanRenderer::setMaterialColor(const plasma::Vector<4, float> &value)
{
    m_uniforms.material = value;
}

void VulkanRenderer::setMatrices(const plasma::Matrix<float> &world, const plasma::Matrix<float> &view,
                                 const plasma::Matrix<float> &projection)
{
    setWorldMatrix(world);
    setViewMatrix(view);
    setProjectionMatrix(projection);
}

void VulkanRenderer::setPointLights(const std::array<plasma::Vector<4, float>, 16> &positions,
                                    const std::array<plasma::Vector<4, float>, 16> &colors)
{
    std::copy_n(positions.begin(), m_uniforms.lightPositions.size(), m_uniforms.lightPositions.begin());
    std::copy_n(colors.begin(), m_uniforms.lightColors.size(), m_uniforms.lightColors.begin());
}

bool VulkanRenderer::backEndStats(float &waitMilliseconds, float &presentMilliseconds, float &gpuMilliseconds,
                                  unsigned &draws, unsigned long long &triangles) const
{
    const VulkanFrameStats &stats = m_context.frameStats();
    waitMilliseconds = stats.waitMilliseconds;
    presentMilliseconds = stats.presentMilliseconds;
    gpuMilliseconds = stats.gpuMilliseconds;
    draws = stats.draws;
    triangles = stats.triangles;
    return true;
}

bool VulkanRenderer::drawSpriteInstances(cube::Sprite *sprite, const SpriteBatch::Instance *const *instances,
                                         size_t count)
{
    m_spriteInstances.resize(count);
    for (size_t i = 0; i < count; ++i) {
        for (int element = 0; element < 16; ++element)
            m_spriteInstances[i].world[element] = instances[i]->matrix[element];
        m_spriteInstances[i].material = instances[i]->color;
        m_spriteInstances[i].alpha = instances[i]->alpha;
    }
    return m_context.drawInstanced(uint32_t(sprite->m_triangleCount) * 3, m_spriteInstances.data(),
                                   uint32_t(m_spriteInstances.size()));
}

void VulkanRenderer::setProjectionMatrix(const plasma::Matrix<float> &matrix)
{
    m_uniforms.projection = matrix;
    m_uniforms.projection.transpose();
}

void VulkanRenderer::setShininess(float value)
{
    m_uniforms.shininess = value;
}

void VulkanRenderer::setSky(const plasma::Vector<4, float> &skyColor1, const plasma::Vector<4, float> &skyColor2,
                            const plasma::Vector<4, float> &fogColor, const plasma::Vector<4, float> &sunColor)
{
    m_uniforms.skyColors = {skyColor1, skyColor2, fogColor, sunColor};
}

void VulkanRenderer::setSunScreenPosition(const plasma::Vector<3, float> &direction, const plasma::Matrix<float> &view,
                                          const plasma::Matrix<float> &projection, const float &cameraPitch, int width,
                                          int height)
{
    const auto position = direction * 100.0f;
    const plasma::Vector<3, float> viewPosition(position[0] * view[0] + position[1] * view[4] + position[2] * view[8],
                                                position[0] * view[1] + position[1] * view[5] + position[2] * view[9],
                                                position[0] * view[2] + position[1] * view[6] + position[2] * view[10]);
    const auto projected = projection * viewPosition;
    const float visible = projected[2] >= 0.0f && projected[2] <= 1.0f ? 1.0f : 0.0f;
    m_uniforms.sunPosition = plasma::Vector<4, float>(projected[0], projected[1] - (cameraPitch - 80.0f) / 30.0f,
                                                      float(width) / float(height), visible);
}

void VulkanRenderer::setViewMatrix(const plasma::Matrix<float> &matrix)
{
    m_uniforms.view = matrix;
    m_uniforms.view.transpose();
}

void VulkanRenderer::setWhite(float value)
{
    m_uniforms.white = value;
}

void VulkanRenderer::setWorldMatrix(const plasma::Matrix<float> &matrix)
{
    m_uniforms.world = matrix;
    m_uniforms.world.transpose();
}

}

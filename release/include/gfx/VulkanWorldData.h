#pragma once

#include <array>
#include <cstdint>
#include "plasma/Matrix.h"
#include "plasma/Vector.h"

namespace gfx {

struct TexturedVertex {
    plasma::Vector<4, float> position;
    plasma::Vector<4, unsigned char> color;
    plasma::Vector<2, float> textureCoordinate;
};

// Constant block shared by the world and effect shaders.
struct VulkanWorldUniforms {
    plasma::Matrix<float> projection;
    plasma::Matrix<float> view;
    plasma::Matrix<float> world;
    plasma::Vector<4, float> material {1, 1, 1, 1};
    std::array<plasma::Vector<4, float>, 10> lightPositions;
    std::array<plasma::Vector<4, float>, 10> lightColors;
    plasma::Vector<4, float> frontColor {1, 1, 1, 1};
    plasma::Vector<4, float> backColor {0, 0, 0, 0};
    plasma::Vector<4, float> lightDirection {0, 0, 1, 1};
    plasma::Vector<4, float> ambient {0, 0, 0, 0};
    std::array<plasma::Vector<4, float>, 4> skyColors;
    plasma::Vector<4, float> sunPosition {0, 0, 1, 0};
    float white = 0;
    float depthOffset = 0;
    float glow = 0;
    uint32_t textureEnabled = 0;
    float fogScale = 0;
    float fogGradient = 0;
    float alpha = 1;
    float shininess = 0;
    // Alpha palette switch, daylight strength, shadow strength, shadow depth bias.
    plasma::Vector<4, float> effects {0, 1, 0, 0};
    plasma::Vector<4, float> sceneCamera {0, 0, 0, 0};
    std::array<plasma::Vector<4, float>, 3> shadowTransform;
    plasma::Vector<4, float> temporalJitter {0, 0, 0, 0};
    // Inverse shadow map size, radius, depth range and inverse RSM size.
    plasma::Vector<4, float> shadowProjection {0, 0, 0, 0};
    // The two world-space wave phases, daylight brightness and submerged camera flag.
    plasma::Vector<4, float> waterSurface {0, 0, 1, 0};
    // The leaves' sway and flutter phases at the world origin, their reach in blocks (0 holds them still), unused.
    plasma::Vector<4, float> foliageWind {0, 0, 0, 0};
};

static_assert(sizeof(VulkanWorldUniforms) == 848);

// One instance of an instanced sprite draw (vertex binding 1, locations 3 to 8): the world matrix's elements in
// plasma's order, which the shaders read as its rows, the material colour and the alpha.
struct VulkanSpriteInstance {
    std::array<float, 16> world;
    plasma::Vector<4, float> material;
    float alpha;
};

static_assert(sizeof(VulkanSpriteInstance) == 84);

}

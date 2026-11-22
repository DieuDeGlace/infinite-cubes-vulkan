#pragma once

#include <glm/glm.hpp>

namespace gfx::alpha {

constexpr double DAY_MS = 86400000.0;

struct ZoneFactors {
    float warmth;
    float moisture;
    float sea;
    float fire;
};

struct Palette {
    glm::vec3 sky1, sky2, fog;
    glm::vec3 front, back, ambient;
};

float paletteDaylight(double timeOfDayMs);
float sunlight(double timeOfDayMs);
glm::vec3 sunDirection(double timeOfDayMs);
Palette palette(float daylight, const ZoneFactors &zone, bool underwater);

}

#pragma once

#include <cstdint>
#include <optional>

namespace cube {
class Field;
}

namespace gfx {

struct WaterLily {
    int x;
    int y;
    int surfaceZ;
    float rotation;
    bool secondModel;
    unsigned char daylight = 255;
};

std::optional<WaterLily> waterLilyForColumn(const cube::Field &field, int x, int y, std::uint64_t seed);

}

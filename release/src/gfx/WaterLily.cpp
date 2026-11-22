#include "gfx/WaterLily.h"

#include <algorithm>
#include "cube/Field.h"
#include "cube/Noise.h"

namespace gfx {
namespace {

std::uint64_t mix(std::uint64_t value)
{
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

std::uint64_t coordinates(int x, int y)
{
    return std::uint32_t(x) | (std::uint64_t(std::uint32_t(y)) << 32);
}

}

std::optional<WaterLily> waterLilyForColumn(const cube::Field &field, int x, int y, std::uint64_t seed)
{
    constexpr int SeaSurface = 1;
    constexpr float MinimumWarmth = 0.2f;
    constexpr float InlandWaterThreshold = 0.5f;
    constexpr int MaximumDepth = 10;

    if (field.m_temperature <= MinimumWarmth || field.m_valleyWater <= InlandWaterThreshold)
        return std::nullopt;

    const auto top = std::find_if(field.m_blocks.rbegin(), field.m_blocks.rend(),
                                  [](const cube::Block &block) { return block.type != cube::BlockAir; });
    if (top == field.m_blocks.rend() || top->type != cube::BlockWater ||
        (top->flags & (cube::BlockFlagNotTerrain | cube::BlockFlagDungeon)))
        return std::nullopt;

    const int surfaceZ = field.m_baseZ + static_cast<int>(top.base() - field.m_blocks.begin());
    if (surfaceZ <= SeaSurface)
        return std::nullopt;

    // Use CWVulkan's eight-block patches and independent seed for the decoration.
    const auto patch = mix(seed ^ coordinates(cube::floorDivide(x, 8), cube::floorDivide(y, 8)));
    if ((patch & 65535u) >= 49152u)
        return std::nullopt;
    const auto sample = mix(seed ^ coordinates(x, y) ^ 0x4c494c59ULL);
    if (sample % 10 != 0)
        return std::nullopt;

    auto floor = top;
    while (floor != field.m_blocks.rend() && floor - top <= MaximumDepth && floor->type == cube::BlockWater)
        ++floor;
    if (floor == field.m_blocks.rend() || floor - top > MaximumDepth || !floor->isSolid())
        return std::nullopt;

    const float rotation = static_cast<float>((sample >> 16) & 65535u) * (360.0f / 65536.0f);
    return WaterLily {x, y, surfaceZ, rotation, (sample & 256u) == 0};
}

}

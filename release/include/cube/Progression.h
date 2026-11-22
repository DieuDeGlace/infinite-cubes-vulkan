#pragma once

#include "cube/WorldIds.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cube::progression {

constexpr int MaxLevel = 1'000'000;

inline int xpToNext(int level)
{
    return 50 + 4 * std::clamp(level, 1, MaxLevel);
}

inline std::uint64_t mix(std::uint64_t value)
{
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

inline int enemyLevel(int playerLevel, int biome, int seed, int x, int y, std::uint64_t creatureId)
{
    int tier = 0;
    switch (biome) {
    case BiomeJungle:
    case BiomeDesert:
    case BiomeDeadlands:
    case BiomeFirelands:
        tier = 2;
        break;
    case BiomeDarkWoods:
    case BiomeSnowForest:
    case BiomeSavannah:
    case BiomeSnowlands:
        tier = 1;
        break;
    }
    const std::uint64_t region =
        mix(std::uint32_t(seed) ^ mix(std::uint32_t(x)) ^ mix(std::uint64_t(std::uint32_t(y)) + 0x9e3779b97f4a7c15ULL));
    const int percent = (tier == 0 ? 90 : tier == 1 ? 108 : 125) + int(region % 9);
    playerLevel = std::clamp(playerLevel, 1, MaxLevel);
    const int level = (playerLevel * percent + 50) / 100 + int(mix(creatureId) % 3) - 1;
    return std::max(tier ? playerLevel + tier : 1, level);
}

inline int killXp(int stars, int enemyLevel, int playerLevel, bool boss = false, bool named = false, bool mini = false)
{
    float xp = float(stars + 1);
    if (playerLevel > enemyLevel)
        xp *= std::pow(float(enemyLevel + 1) / playerLevel, 3.0f);
    else {
        const int difference = std::min(20, enemyLevel - playerLevel);
        xp *= (1.0f + difference / 6.0f) * std::pow(1.05f, float(difference));
    }
    if (boss)
        xp *= 10;
    if (named)
        xp *= 5;
    if (mini)
        xp *= 2;
    return std::max(1, int(xp));
}

inline float creatureScale(int level)
{
    return 1.0f + 1000 * std::log2((level + 1001.0f) / 1000.0f);
}

inline int lootLevel(int playerLevel, int enemyLevel, int category, unsigned int id, unsigned int modifier,
                     int material)
{
    std::uint64_t roll = 0;
    for (const auto value : {unsigned(category), id, modifier, unsigned(material)})
        roll = mix(roll ^ value);
    if (enemyLevel > playerLevel && mix(roll) % 3 == 0)
        return std::min(MaxLevel, playerLevel + 1 + int(mix(roll + 1) % (enemyLevel - playerLevel)));
    return std::max(1, playerLevel - int(roll % 3));
}

inline float gearStrength(int level, int rarity, std::uint32_t modifier)
{
    auto shifted = std::int32_t(modifier ^ (modifier << 13));
    shifted ^= shifted >> 17;
    shifted = std::int32_t(std::uint32_t(shifted) ^ (std::uint32_t(shifted) << 5));
    const float shift = float(shifted / 0x10624DD3) / 8.0f;
    return 1000 * std::log2((level + 3 * (rarity + shift) + 1001.0f) / 1000.0f);
}

} // namespace cube::progression

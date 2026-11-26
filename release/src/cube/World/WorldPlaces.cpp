#include "cube/World.h"
#include "cube/AlphaTerrain.h"
#include "cube/WorldIds.h"
#include "cube/WorldLayout.h"
#include <cmath>
#include <limits>

namespace cube {

int BlocksPerPlace = NativeBlocksPerPlace;

namespace {

plasma::Vector<2, long long> nativeLandCenter(int x, int y)
{
    constexpr int Margin = 1024;
    constexpr int Spread = BlocksPerLand - 2 * Margin;
    return plasma::Vector<2, long long>(
        static_cast<long long>(x) * BlocksPerLand + Margin + random(x + 58014, y) % Spread,
        static_cast<long long>(y) * BlocksPerLand + Margin + random(x + 460082, y) % Spread);
}

} // namespace

bool World::isOrdinaryTree(int type)
{
    return type == BuildingOak || type == BuildingEvergreen || type == BuildingBirch || type == BuildingJungleTree ||
           type == BuildingPalm;
}

float World::terrainHeight(int x, int y, int mode)
{
    return sampleHeight(x, y, mode, m_alphaTerrain != nullptr);
}

float World::terrainOffset(int x, int y, float nativeHeight, const BiomeSample &biomes, const AreaInfo &area,
                           float roadDistance, float *rockiness)
{
    if (!m_alphaTerrain) {
        if (rockiness)
            *rockiness = 0.0f;
        return 0.0f;
    }
    return m_alphaTerrain->heightOffset(*this, x, y, nativeHeight, biomes, area, roadDistance, rockiness);
}

float World::channelDistance(int x, int y, const BiomeSample &biomes, const AreaInfo &area)
{
    // A bed is flat where the channel's weight reaches this.
    constexpr float FlatWeight = 0.30f;
    // The first-order estimate runs long where the noise steepens towards the bed.
    constexpr float Trust = 0.8f;
    // About the steepest change per block of the firelands and undead weights at a biome border, doubled as the
    // strengths double them.
    constexpr float StrengthGradient = 0.006f;
    // Past this many blocks the relief is not limited, so the ridge noise is not sampled around.
    constexpr float Reach = 600.0f;
    constexpr int Step = 2;

    BiomeSample sample = biomes;
    const float firelands = sample.lavaWeight(area.type, area.influence);
    const float undead = sample.undead(area.type, area.influence);
    const float mountains = sample.mountains();
    const float wetlands = sample.wetness();

    float nearest = Reach;
    // strength: the channel's weight on its centre line. fading: a linear stand-in that keeps falling past the
    // border where the strength reaches 0, so the distance to where the channel fades in stays continuous there.
    const auto measure = [&](float strength, float fading, auto ridgeAt) {
        const float fadeIn = (FlatWeight - fading) / StrengthGradient;
        if (fadeIn >= nearest)
            return;
        // The ridge at which the profile times the strength reaches FlatWeight (channelProfile inverted below its
        // knee). A fading channel's bed narrows to its centre line.
        const float flat = strength > FlatWeight ? 1.0f - std::sqrt(FlatWeight / strength) : 0.0f;
        const float centre = ridgeAt(x, y) - flat;
        float distance = centre;
        if (centre > 0.0f) {
            // The steeper one-sided difference on each axis stays continuous across the noise's creases (tributary
            // lines), where a single difference would jump between the two sides' slopes.
            const auto slope = [&](int dx, int dy) {
                const float before = ridgeAt(x - dx, y - dy), after = ridgeAt(x + dx, y + dy);
                return (std::max)(std::abs(after - centre - flat), std::abs(centre + flat - before)) / float(Step);
            };
            // Where a channel fades the threshold itself moves: add its steepest rate, the derivative of flat at the
            // strength (held at the tip's below FlatWeight), times the strength's.
            const float tip = (std::max)(strength, FlatWeight);
            const float thresholdSlope = 0.5f * std::sqrt(FlatWeight / tip) / tip * StrengthGradient;
            const float gradient = std::hypot(slope(Step, 0), slope(0, Step)) + thresholdSlope;
            distance = centre / gradient * Trust;
        }
        nearest = (std::min)(nearest, (std::max)(distance, fadeIn));
    };
    measure((1.0f - (std::min)(firelands + firelands, 1.0f)) * (1.0f - (std::min)(undead + undead, 1.0f)),
            1.0f - 2.0f * (firelands + undead), [&](int px, int py) { return riverRidge(px, py, wetlands); });
    const float lava = (firelands - 0.5f) * 2.0f * (1.0f - mountains);
    measure(lava, lava, [&](int px, int py) { return lavaRidge(px, py); });
    const float poison = (undead - 0.5f) * 2.0f * (1.0f - mountains);
    measure(poison, poison, [&](int px, int py) { return poisonRidge(px, py); });
    return nearest;
}

template<typename Test> bool World::cachedTreeTest(TreeCache &cache, int cellX, int cellY, int type, Test test)
{
    // Zone and map workers ask about the same trees many times.
    auto &entry = cache[(unsigned(cellX) * 31 + unsigned(cellY)) % cache.size()];
    if (entry.world != this || entry.seed != m_seed || entry.cellX != cellX || entry.cellY != cellY ||
        entry.type != type || entry.largerBiomes != m_largerBiomes)
        entry = {this, m_seed, cellX, cellY, type, m_largerBiomes, test()};
    return entry.result;
}

bool World::nearVillage(int x, int y, float margin)
{
    const int cellX = floorDivide(x, BlocksPerPlace);
    const int cellY = floorDivide(y, BlocksPerPlace);
    for (int i = cellX - 1; i <= cellX + 1; ++i) {
        for (int j = cellY - 1; j <= cellY + 1; ++j) {
            const bool large = isLargePlace(i, j);
            const auto center = placeCenter(i, j, large);
            const double reach = double(large ? LargePlaceRadius : SmallPlaceRadius) + margin;
            const double dx = double(x) - center[0], dy = double(y) - center[1];
            if (dx * dx + dy * dy < reach * reach && locationType(i, j, large) == PlaceVillage)
                return true;
        }
    }
    return false;
}

bool World::treeFitsTerrain(int cellX, int cellY, int type)
{
    if (!m_alphaTerrain || !isOrdinaryTree(type))
        return true;
    static thread_local TreeCache cache;
    return cachedTreeTest(cache, cellX, cellY, type, [&] {
        if (!treeClearsSlope(cellX, cellY, type))
            return false;
        // Of two trees whose crowns would merge, keep the one with the higher hash.
        const auto point = streetPoint(cellX, cellY);
        const float reach = crownRadius(cellX, cellY, type);
        const unsigned int priority = random(cellX + 873211, cellY);
        for (int i = cellX - 1; i <= cellX + 1; ++i) {
            for (int j = cellY - 1; j <= cellY + 1; ++j) {
                if (i == cellX && j == cellY)
                    continue;
                const unsigned int other = random(i + 873211, j);
                if (other < priority || (other == priority && (i < cellX || (i == cellX && j < cellY))))
                    continue;
                const int otherType = zoneBuildingType(i, j);
                if (!isOrdinaryTree(otherType))
                    continue;
                const auto otherPoint = streetPoint(i, j);
                const float dx = float(otherPoint[0] - point[0]);
                const float dy = float(otherPoint[1] - point[1]);
                const float apart = (reach + crownRadius(i, j, otherType)) * CrownSpacing;
                if (dx * dx + dy * dy < apart * apart && treeClearsSlope(i, j, otherType))
                    return false;
            }
        }
        return true;
    });
}

bool World::treeClearsSlope(int cellX, int cellY, int type)
{
    static thread_local TreeCache cache;
    return cachedTreeTest(cache, cellX, cellY, type, [&] {
        // The crown spreads about half the tree's extent and starts about halfway up the trunk. Gentle slopes keep
        // their trees; where the ground beside the trunk rises past most of the trunk the crown sinks into the hill.
        const auto point = streetPoint(cellX, cellY);
        const float ground = terrainHeight(point[0], point[1], HeightFlagCaveMouths);
        const float reach = crownRadius(cellX, cellY, type) * 0.9f;
        float allowed = float(townSizeC(cellX, cellY, 1.0f)) * 0.8f;
        if (type == BuildingEvergreen)
            allowed *= 1.5f;
        constexpr float Diagonal = 0.70710678f;
        constexpr float Directions[8][2] = {{1, 0},  {Diagonal, Diagonal},   {0, 1},  {-Diagonal, Diagonal},
                                            {-1, 0}, {-Diagonal, -Diagonal}, {0, -1}, {Diagonal, -Diagonal}};
        for (const auto &direction : Directions) {
            const int x = point[0] + int(std::lround(direction[0] * reach));
            const int y = point[1] + int(std::lround(direction[1] * reach));
            if (terrainHeight(x, y, 0) - ground > allowed)
                return false;
        }
        return true;
    });
}

float World::crownRadius(int cellX, int cellY, int type)
{
    // As drawn on the map: birches are narrower.
    const float radius = float(townSizeD(cellX, cellY, 1.0f)) * 0.5f;
    return type == BuildingBirch ? radius * 0.75f : radius;
}

void World::biomeSample(int x, int y, BiomeSample &sample)
{
    sampleLands(x, y, &sample);
}

plasma::Vector<2, int> World::findNearestRegion(int x, int y)
{
    return sampleLands(x, y, nullptr);
}

plasma::Vector<2, int> World::landCenter(int x, int y) const
{
    const auto center = nativeLandCenter(x, y);
    const double scale = double(blocksPerLand()) / BlocksPerLand;
    const auto toWorld = [scale](long long value) {
        const double position = std::floor(value * scale);
        return int(
            std::clamp(position, double(std::numeric_limits<int>::min()), double(std::numeric_limits<int>::max())));
    };
    return plasma::Vector<2, int>(toWorld(center[0]), toWorld(center[1]));
}

plasma::Vector<2, int> World::sampleLands(int x, int y, BiomeSample *sample)
{
    const double scale = double(blocksPerLand()) / BlocksPerLand;
    // As in CWVulkan, scale the biome lookup before displacing it with border noise.
    // Elevation details and place logic still use the original world coordinates.
    const int sx = m_largerBiomes ? int(std::floor(x / scale)) : x;
    const int sy = m_largerBiomes ? int(std::floor(y / scale)) : y;
    const float offsetX = noise(sx * 0.0003 + 8432.0, sy * 0.0006 + 9438.0) * 0.125f * BlocksPerLand;
    const float offsetY = noise(sx * 0.0006 + 567.0, sy * 0.0003 + 1223.0) * 0.125f * BlocksPerLand;
    const Fixed positionX = Fixed(sx) + offsetX;
    const Fixed positionY = Fixed(sy) + offsetY;
    const int minX = int(std::floor(double(positionX) / BlocksPerLand)) - 1;
    const int minY = int(std::floor(double(positionY) / BlocksPerLand)) - 1;
    const float squaredScale = float(scale * scale);
    float nearest = -1.0f;
    plasma::Vector<2, int> region;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            const int rx = minX + i;
            const int ry = minY + j;
            const auto center = nativeLandCenter(rx, ry);
            const float dx = float(positionX - Fixed(center[0] * FixedUnitsPerBlock));
            const float dy = float(positionY - Fixed(center[1] * FixedUnitsPerBlock));
            const float squaredDistance = (dy * dy + dx * dx) * squaredScale;
            if (sample) {
                sample->biomes[i][j] = landBiome(rx, ry);
                sample->squaredDistances[i][j] = squaredDistance;
            }
            if (nearest < 0.0f || squaredDistance < nearest) {
                nearest = squaredDistance;
                region = plasma::Vector<2, int>(rx, ry);
                if (sample)
                    sample->nearestBiome = sample->biomes[i][j];
            }
        }
    }
    if (sample) {
        sample->x = x;
        sample->y = y;
        sample->nearestSquaredDistance = nearest;
    }
    return region;
}

float World::height(int x, int y, int mode)
{
    return sampleHeight(x, y, mode, false);
}

bool World::hasSurfaceWater(int x, int y, int bottom, int top)
{
    int waterEnd;
    const int ground = (int)std::floor(sampleHeight(x, y, 0, false, &waterEnd));
    const int waterStart = waterEnd > 1 ? ground : ground + 1;
    return (std::max)(bottom, waterStart) <= (std::min)(top, waterEnd - 1);
}

float World::sampleHeight(int x, int y, int mode, bool useRelief, int *waterEnd)
{
    BiomeSample sample;
    biomeSample(x, y, sample);
    AreaInfo area;
    fillAreaInfo(x, y, area);
    float mountains = sample.mountains();
    float elevation = sample.biomeElevation(area.type, area.influence);
    float roadDistance = 0.0f;
    float road = streetAmount(x, y, area.type, area.influence, &roadDistance);
    float firelands = sample.lavaWeight(area.type, area.influence);
    float undead = sample.undead(area.type, area.influence);
    float river = valleyWater(x, y, road, firelands, undead, sample.wetness(), mountains, &area);
    float lavaRiver = lavaChannels(x, y, road, firelands, mountains, &area);
    float poisonRiver = poisonChannels(x, y, road, undead, mountains, &area);
    float cliffs = mountainCliffs(x, y, elevation, mountains, river, lavaRiver, poisonRiver, road, &area);
    float desertness = sample.desertness();
    float sand = sandAmount(x, y, sample.humidity(), cliffs, desertness);
    const float native =
        height(x, y, elevation, cliffs, river, lavaRiver, poisonRiver, sand, road, mountains, &area, mode);
    if (waterEnd) {
        *waterEnd = 1; // The sea surface is block zero.
        if (river > 0.5f && native > -5.0f)
            *waterEnd = (std::max)(1, (int)std::floor(villageHeight(x, y, elevation, &area)) - 1);
    }
    if (!useRelief)
        return native;
    return native + terrainOffset(x, y, native, sample, area, roadDistance);
}

bool World::isLargePlace(int x, int y)
{
    // A hash of the cell pair (salt 3776829): one pair in two is large.
    if ((random(floorDivide(x, 2) + 3776829, floorDivide(y, 2)) & 1) == 0 || isLargePlaceAlways(x, y)) {
        return true;
    }
    return false;
}

bool World::isLargePlaceAlways(int x, int y)
{
    // The cell pairs on the diagonal lattice x + 5y = 1 (mod 7): one in seven.
    return ((floorDivide(x, 2) + floorDivide(y, 2) * 5) % 7 + 7) % 7 == 1;
}

}

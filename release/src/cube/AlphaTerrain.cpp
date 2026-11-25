#include "cube/AlphaTerrain.h"
#include "cube/WorldLayout.h"
#include "cube/ZoneIds.h"
#include "alphagen/Coordinates.h"
#include "alphagen/Random.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace cube {
namespace {

constexpr unsigned int LandscapeSeed = 1570745101;
constexpr int RockRadius = 80;
constexpr int ShoreClearance = 20;
constexpr float ReliefCenter = 48.0f;
constexpr float MaximumRise = 160.0f;
// The strongest biome scale (mountains) on the highest relief.
constexpr float MaximumLift = MaximumRise * 1.25f;
// Relief may rise at most this much per block away from a road, place or landmark it must leave flat. Fading the full
// relief over a fixed band cut straight cliffs of three to eight blocks per block along roads and footprint edges.
constexpr float MaximumBlendSlope = 1.1f;
// The cap starts level at the edge and reaches its full slope this many blocks out.
constexpr float BlendRamp = 24.0f;
// Past this distance from a landmark the cap no longer limits any relief.
constexpr int AnchorReach = int(MaximumLift / MaximumBlendSlope + BlendRamp) + 1;
// Smooth the crease where the capped ramp meets the relief noise.
constexpr float CapSmoothing = 24.0f;
// Roads stay flat out to where Steam's road weight, (1 - d^2 / 250^2)^16, gives a street amount of 0.40. The roads'
// noise warp can squeeze distances, so trust 70% of the warped distance beyond that.
constexpr float RoadFlatDistance = 29.0f;
constexpr float RoadDistanceTrust = 0.7f;
// Places stay flat where their influence passes 0.12, at 81% of their radius; elemental rings out to their radius.
constexpr float PlaceFlatFraction = 0.81f;
// Above the shore the relief may grow by this much per block the native ground rises. A fade over a fixed height band
// multiplied steep native slopes by up to ten and raised them into cliffs.
constexpr float ShoreReliefSlope = 1.5f;
// Cliffs show full stone where the relief may rise this many blocks, and the native earth where it must stay flat.
constexpr float RockFade = 6.0f;
constexpr int CellsPerZone = BlocksPerZone / BlocksPerCell;

struct Footprint {
    int x, y, radius;
};

// The fixed footprints of a zone's own cells.
std::vector<Footprint> zoneFeatures(World &world, int zoneX, int zoneY)
{
    std::vector<Footprint> features;
    for (int dx = 0; dx < CellsPerZone; ++dx) {
        for (int dy = 0; dy < CellsPerZone; ++dy) {
            const int cx = zoneX * CellsPerZone + dx;
            const int cy = zoneY * CellsPerZone + dy;
            const int type = world.zoneBuildingType(cx, cy);
            if ((type == BuildingNone || World::isOrdinaryTree(type)) &&
                world.structureCell(cx, cy, true).kind == StructureCell_None)
                continue;

            int radius = (buildingReach(type) + 1) * BlocksPerCell;
            if (type == BuildingRock || type == BuildingTallRock || type == BuildingRockLorePavilion ||
                type == BuildingRockTreasure)
                radius = RockRadius + BlocksPerCell;
            else if (type == BuildingGreatOak || type == BuildingHill || type == BuildingCave)
                radius = RockRadius;
            features.push_back(
                {cx * BlocksPerCell + BlocksPerCell / 2, cy * BlocksPerCell + BlocksPerCell / 2, radius});
        }
    }
    return features;
}

// zoneFeatures, kept per thread. The map's columns lie a zone or more apart, so each of them needs a new
// neighbourhood (fixedFeatures) whose zones its neighbours have mostly read already.
const std::vector<Footprint> &cachedZoneFeatures(World &world, int zoneX, int zoneY)
{
    struct Entry {
        const World *world = nullptr;
        int seed = 0, zoneX = 0, zoneY = 0;
        bool largerBiomes = false;
        std::vector<Footprint> features;
    };
    static thread_local std::array<Entry, 1024> cache;
    auto &entry = cache[(unsigned(zoneX) * 31 + unsigned(zoneY)) % cache.size()];
    if (entry.world != &world || entry.seed != world.m_seed || entry.zoneX != zoneX || entry.zoneY != zoneY ||
        entry.largerBiomes != world.m_largerBiomes)
        entry = {&world, world.m_seed, zoneX, zoneY, world.m_largerBiomes, zoneFeatures(world, zoneX, zoneY)};
    return entry.features;
}

// The fixed footprints of the cells within Padding cells of a zone: any of them can reach one of its columns.
std::vector<Footprint> fixedFeatures(World &world, int zoneX, int zoneY)
{
    constexpr int Margin = AnchorReach + RockRadius + BlocksPerCell;
    constexpr int Padding = Margin / BlocksPerCell;
    const int firstX = zoneX * CellsPerZone - Padding;
    const int endX = (zoneX + 1) * CellsPerZone + Padding;
    const int firstY = zoneY * CellsPerZone - Padding;
    const int endY = (zoneY + 1) * CellsPerZone + Padding;
    std::vector<Footprint> features;
    for (int x = floorDivide(firstX, CellsPerZone); x <= floorDivide(endX - 1, CellsPerZone); ++x) {
        for (int y = floorDivide(firstY, CellsPerZone); y <= floorDivide(endY - 1, CellsPerZone); ++y) {
            for (const Footprint &feature : cachedZoneFeatures(world, x, y)) {
                const int cellX = floorDivide(feature.x, BlocksPerCell);
                const int cellY = floorDivide(feature.y, BlocksPerCell);
                if (cellX >= firstX && cellX < endX && cellY >= firstY && cellY < endY)
                    features.push_back(feature);
            }
        }
    }
    return features;
}

float fadeIn(float value, float start, float end)
{
    const float t = std::clamp((value - start) / (end - start), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Blocks from (x, y) to the nearest fixed footprint, up to AnchorReach.
float featureDistance(World &world, int x, int y)
{
    // Generation and map workers query the same footprints without depending on loaded zones.
    struct Entry {
        const World *world = nullptr;
        int seed = 0, zoneX = 0, zoneY = 0;
        bool largerBiomes = false;
        std::vector<Footprint> features;
    };
    static thread_local std::array<Entry, 64> cache;
    const int zoneX = floorDivide(x, BlocksPerZone);
    const int zoneY = floorDivide(y, BlocksPerZone);
    auto &entry = cache[(unsigned(zoneX) * 17 + unsigned(zoneY)) % cache.size()];
    if (entry.world != &world || entry.seed != world.m_seed || entry.zoneX != zoneX || entry.zoneY != zoneY ||
        entry.largerBiomes != world.m_largerBiomes)
        entry = {&world, world.m_seed, zoneX, zoneY, world.m_largerBiomes, fixedFeatures(world, zoneX, zoneY)};

    // Round footprints. Box footprints left straight ramps that read as sliced hillsides.
    float nearest = float(AnchorReach);
    for (const Footprint &feature : entry.features) {
        const float dx = float(std::abs(x - feature.x));
        const float dy = float(std::abs(y - feature.y));
        const float radius = float(feature.radius);
        if (dx - radius >= nearest || dy - radius >= nearest)
            continue;
        const float gap = std::sqrt(dx * dx + dy * dy) - radius;
        if (gap <= 0.0f)
            return 0.0f;
        nearest = (std::min)(nearest, gap);
    }
    return nearest;
}

// Blocks beyond the flat core of the nearest place. Checks the neighbouring place cells as well, so the cap does not
// jump where a column's own cell has no place but the next cell's place lies close by.
float placeDistance(World &world, int x, int y)
{
    struct Place {
        plasma::Vector<2, int> center;
        float core; // the radius of the flat core
    };
    // The places of the 3 x 3 cells around a cell, shared by its columns.
    struct Entry {
        const World *world = nullptr;
        int seed = 0, cellX = 0, cellY = 0;
        std::vector<Place> places;
    };
    static thread_local std::array<Entry, 16> cache;
    const int cellX = floorDivide(x, BlocksPerPlace);
    const int cellY = floorDivide(y, BlocksPerPlace);
    auto &entry = cache[(unsigned(cellX) * 7 + unsigned(cellY)) % cache.size()];
    if (entry.world != &world || entry.seed != world.m_seed || entry.cellX != cellX || entry.cellY != cellY) {
        entry = {&world, world.m_seed, cellX, cellY, {}};
        for (int i = cellX - 1; i <= cellX + 1; ++i) {
            for (int j = cellY - 1; j <= cellY + 1; ++j) {
                const bool large = world.isLargePlace(i, j);
                const int type = world.locationType(i, j, large);
                if (type == PlaceNone)
                    continue;
                const float radius = float(large ? LargePlaceRadius : SmallPlaceRadius);
                // An elemental ring's water runs out to the full radius.
                const bool ring = world.environmentType(i, j, type, large) == EnvironmentTypeElementalRing;
                entry.places.push_back({world.placeCenter(i, j, large), ring ? radius : radius * PlaceFlatFraction});
            }
        }
    }

    float nearest = float(AnchorReach);
    for (const Place &place : entry.places) {
        const float dx = float(x - place.center[0]);
        const float dy = float(y - place.center[1]);
        // The border noise moves the edge by at most PlaceBorderNoiseReach along each axis.
        if (std::sqrt(dx * dx + dy * dy) - place.core - float(PlaceBorderNoiseReach) * 1.5f >= nearest)
            continue;
        const float gap = std::sqrt(world.placeSquaredDistance(place.center, x, y)) - place.core;
        if (gap <= 0.0f)
            return 0.0f;
        nearest = (std::min)(nearest, gap);
    }
    return nearest;
}

// The highest relief allowed this far from a flat edge: level at the edge, then MaximumBlendSlope per block.
float liftCap(float distance)
{
    if (distance <= 0.0f)
        return 0.0f;
    if (distance < BlendRamp * 2.0f)
        return MaximumBlendSlope * distance * distance / (BlendRamp * 4.0f);
    return MaximumBlendSlope * (distance - BlendRamp);
}

// alphagen::noise with the lattice cell found by floor. Alpha truncated towards zero, which only matched floor on its
// positive coordinates: below zero each lattice line became a seam where the value jumps, cutting straight cliffs into
// the relief every 100 to 5,000 blocks. Equal to alphagen::noise wherever both coordinates are positive.
float latticeNoise(double x, double y)
{
    constexpr double Pi = 3.1415927; // Alpha's double
    const double fx = std::floor(x), fy = std::floor(y);
    const uint32_t ux = uint32_t(int32_t(fx)), uy = uint32_t(int32_t(fy));
    const auto value = [](uint32_t n) { return alphagen::latticeValue(int32_t(n)); };
    const double wx = (1.0 - std::cos((x - fx) * Pi)) * 0.5;
    const double wy = (1.0 - std::cos((y - fy) * Pi)) * 0.5;
    const double row = value(ux + 57u * uy) * (1.0 - wx) + value(ux + 1u + 57u * uy) * wx;
    const double nextRow = value(ux + 57u * (uy + 1u)) * (1.0 - wx) + value(ux + 1u + 57u * (uy + 1u)) * wx;
    return float(row * (1.0 - wy) + nextRow * wy);
}

float smoothMin(float a, float b, float smoothing)
{
    const float h = (std::max)(smoothing - std::abs(a - b), 0.0f) / smoothing;
    return (std::min)(a, b) - h * h * smoothing * 0.25f;
}

} // namespace

struct AlphaTerrain::Data {
    const alphagen::SeedOffsets offsets = alphagen::makeSeedOffsets(LandscapeSeed);

    float relief(int x, int y) const
    {
        const double ax = double(x) + alphacoords::HOME;
        const double ay = double(y) + alphacoords::HOME;
        const auto noise = [&](int ox, int oy, double scale) {
            return latticeNoise(offsets[ox] + ax * scale, offsets[oy] + ay * scale);
        };
        const auto weight = [&](int ox, int oy, double scale) {
            const float value = alphagen::unit(noise(ox, oy, scale));
            return value * value;
        };

        // The broad hills and roughness terms from the Alpha height formula.
        const float broad = (noise(0x1bc, 0x1c0, 0.0002) + 1.0f) * 100.0f * weight(0x194, 0x198, 0.0001) +
                            (noise(0x1b4, 0x1b8, 0.0002) + 1.0f) * 100.0f * weight(0x18c, 0x190, 0.0001);
        const float roughness = std::abs(noise(0x168, 0x16c, 0.001) + 0.1f * noise(0x170, 0x174, 0.01));
        const float gate = fadeIn(roughness, 0.0f, 0.25f);
        const float hills = (noise(0x1cc, 0x1d0, 0.002) + 1.0f) * 50.0f * weight(0x1a4, 0x1a8, 0.001) +
                            (noise(0x1c4, 0x1c8, 0.002) + 1.0f) * 50.0f * weight(0x19c, 0x1a0, 0.001) +
                            (noise(0x1d4, 0x1d8, 0.01) + 1.0f) * 20.0f * weight(0x1ac, 0x1b0, 0.002);
        return std::clamp(broad + gate * gate * hills - ReliefCenter, -ReliefCenter, MaximumRise);
    }
};

AlphaTerrain::AlphaTerrain()
    : m_data(std::make_unique<Data>())
{}

AlphaTerrain::~AlphaTerrain() = default;

float AlphaTerrain::heightOffset(World &world, int x, int y, float height, const World::BiomeSample &biomes,
                                 const World::AreaInfo &area, float roadDistance, float *rockiness) const
{
    if (rockiness)
        *rockiness = 0.0f;
    // Roads, places, landmarks and channel beds keep their native grade, and the shore keeps its height. The relief
    // climbs away from each at a bounded slope, so tall hills ramp down instead of being cut off at the edge of a blend
    // band. Cheapest bounds first.
    const float room = ShoreReliefSlope * (height - ShoreClearance);
    float cap = (std::min)(room, liftCap((roadDistance - RoadFlatDistance) * RoadDistanceTrust));
    if (cap <= 0.0f)
        return 0.0f;
    const float relief = m_data->relief(x, y) * biomes.alphaReliefStrength();
    if (relief == 0.0f)
        return 0.0f;
    const auto tighten = [&cap](float distance) {
        cap = (std::min)(cap, liftCap(distance));
        return cap > 0.0f;
    };
    if (!tighten(world.channelDistance(x, y, biomes, area)) || !tighten(placeDistance(world, x, y)) ||
        !tighten(featureDistance(world, x, y)))
        return 0.0f;
    // The bound grows smoothly away from roads, places and the shore, so cliffs fade from stone to the native earth
    // over a few blocks there. The relief's own value crosses zero inside hill country and would leave earthen bands.
    if (rockiness)
        *rockiness = fadeIn(cap, 0.0f, RockFade);

    const float magnitude = (std::max)(0.0f, smoothMin(std::abs(relief), cap, (std::min)(CapSmoothing, cap)));
    // Keep valleys above the shore. Quantize only the combined height when writing voxel columns.
    return (std::max)(relief < 0.0f ? -magnitude : magnitude, ShoreClearance - height);
}

} // namespace cube

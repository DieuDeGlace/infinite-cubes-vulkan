#include "cube/World.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>

namespace cube {
namespace {

using Position = plasma::Vector<3, Fixed>;
using Size = plasma::Vector<3, float>;

constexpr float RadiansPerDegree = 0.017453292f;

std::uint32_t waterHash(int x, int y, int seed)
{
    std::uint32_t hash = std::uint32_t(x) * 0x9e3779b9u ^ std::uint32_t(y) * 0x85ebca6bu ^ std::uint32_t(seed);
    hash = (hash ^ (hash >> 16)) * 0x7feb352du;
    return hash ^ (hash >> 15);
}

bool passiveFish(int race)
{
    return race == RaceSapphireFish || race == RaceLemonFish || race == RaceSeahorse || race == RaceLanternFish ||
           race == RaceBlowfish || race == RaceSnail;
}

bool bottomDweller(int race)
{
    return race == RaceCrab || race == RaceSeaCrab || race == RaceSpitter ||
           (race >= RaceGreenSlime && race <= RaceBlueSlime);
}

bool waterBlock(const Block &block)
{
    return block.type == BlockWater && !(block.flags & (BlockFlagDungeon | BlockFlagProtected));
}

template<typename BlockAt> bool fitsInWater(const Position &position, const Size &size, BlockAt blockAt)
{
    for (int x = (position[0] - size[0] * 0.5f).toBlock(); x <= (position[0] + size[0] * 0.5f).toBlock(); ++x) {
        for (int y = (position[1] - size[1] * 0.5f).toBlock(); y <= (position[1] + size[1] * 0.5f).toBlock(); ++y) {
            for (int z = (position[2] - size[2] * 0.5f).toBlock(); z <= (position[2] + size[2] * 0.5f).toBlock(); ++z) {
                if (!waterBlock(blockAt(x, y, z)))
                    return false;
            }
        }
    }
    return true;
}

bool waterColumn(Zone &zone, int x, int y, int &floor, int &surface)
{
    const Field *field = zone.field(x, y);
    if (!field)
        return false;
    // Zone::getBlock supplies implicit ocean water above the stored seafloor.
    int top = std::max(0, field->m_baseZ + int(field->size()) - 1);
    while (top >= field->m_baseZ) {
        const Block block = zone.getBlock(x, y, top);
        if (waterBlock(block))
            break;
        if (block.isSolid())
            return false;
        --top;
    }
    int bottom = top;
    while (bottom >= field->m_baseZ && waterBlock(zone.getBlock(x, y, bottom)))
        --bottom;
    floor = bottom + 1;
    surface = top + 1;
    return surface - floor >= 2;
}

bool swimPath(World &world, const Position &start, const Position &destination, const Size &size)
{
    const auto path = destination - start;
    const float distance = Size(path).length();
    if (distance > 24.0f)
        return false;
    const int steps = std::max(1, int(std::ceil(distance * 2.0f)));
    for (int i = 1; i <= steps; ++i) {
        if (!fitsInWater(start + path * (float(i) / steps), size,
                         [&](int x, int y, int z) { return world.getBlock(x, y, z); }))
            return false;
    }
    return true;
}

} // namespace

void Zone::addAquaticResidents()
{
    constexpr int SeaRaces[] = {RaceSeaCrab,     RaceSapphireFish, RaceLemonFish, RaceSeahorse, RaceMerman, RaceShark,
                                RaceLanternFish, RaceMawFish,      RacePiranha,   RaceBlowfish, RaceSnail};
    constexpr int InlandRaces[] = {RaceCrab,        RaceSapphireFish, RaceLemonFish, RaceSeahorse, RaceShark,
                                   RaceLanternFish, RaceMawFish,      RacePiranha,   RaceBlowfish, RaceSnail};
    constexpr int BossRaces[] = {RaceCrocodile, RaceShark, RaceMawFish, RacePiranha, RaceSeaCrab};
    constexpr std::array<std::array<int, 2>, 7> SchoolOffsets = {
        {{0, 0}, {3, 1}, {-3, -1}, {1, -3}, {-1, 3}, {4, -3}, {-4, 3}}};
    const int zx = m_position[0];
    const int zy = m_position[1];
    const auto roll = waterHash(zx, zy, m_world->m_seed);
    const int startX = zx * BlocksPerZone;
    const int startY = zy * BlocksPerZone;

    for (int probe = 0; probe < 16; ++probe) {
        const unsigned slot = (probe + roll % 16) % 16;
        const int x = startX + 8 + int(slot % 4) * 16;
        const int y = startY + 8 + int(slot / 4) * 16;
        int floor, surface;
        if (!waterColumn(*this, x, y, floor, surface))
            continue;
        const int depth = surface - floor;
        const bool ocean = depth >= 6 && m_world->landBiome(m_world->findNearestRegion(x, y)) == BiomeOcean;
        if (ocean ? modulo(zx + zy, 2) != 0 : modulo(zx + zy + m_world->m_seed % 3, 3) != 0)
            continue;

        const bool boss = (roll >> 16) % 4 == 0 && depth >= 6;
        int race =
            ocean ? SeaRaces[(roll >> 8) % std::size(SeaRaces)] : InlandRaces[(roll >> 8) % std::size(InlandRaces)];
        if (boss) {
            race = BossRaces[(roll >> 4) % (ocean ? 5 : 4)];
        } else if (!ocean && (roll >> 20) % 3 == 0) {
            const auto lake = waterHash(floorDivide(x, 128), floorDivide(y, 128), m_world->m_seed);
            race = lake % 3 == 0 ? RaceCrab : lake % 3 == 1 ? RaceSpitter : RaceGreenSlime + int((lake / 3) % 4);
        }

        std::vector<std::unique_ptr<Resident>> school;
        const unsigned wanted = boss ? 1 : 2 + (roll >> 5) % 2;
        for (const auto &offset : SchoolOffsets) {
            const int px = x + offset[0];
            const int py = y + offset[1];
            int low, high;
            if (!waterColumn(*this, px, py, low, high))
                continue;
            auto resident = std::make_unique<Resident>();
            resident->race = race;
            resident->hostility = passiveFish(race) ? HostilityNeutral : HostilityEnemy;
            resident->level = boss ? 4 : int((roll >> 11) % 3);
            fillAppearance(resident->race, resident->appearance, nullptr, false);
            auto &appearance = resident->appearance;
            appearance.appearanceFlags |= AppearanceFlagAquatic;
            if (boss) {
                const float scale = race == RacePiranha ? 2.5f : 2.0f;
                appearance.size *= scale;
                appearance.headScale *= scale;
                appearance.chestScale *= scale;
                appearance.feetScale *= scale;
                appearance.handsScale *= scale;
                appearance.tailScale *= scale;
                appearance.appearanceFlags |= AppearanceFlagLarge;
            }
            const float halfHeight = appearance.size[2] * 0.5f + 0.1f;
            if (low + halfHeight >= high - halfHeight)
                continue;
            const int columnDepth = high - low;
            int belowSurface = 2 + int(roll % 3);
            if (ocean && columnDepth >= 8 && (roll >> 4) % 2 != 0)
                belowSurface = columnDepth / 4 + int((roll >> 12) % (columnDepth - columnDepth / 4 - 1));
            const float z = bottomDweller(race)
                                ? low + halfHeight
                                : std::clamp(float(high - belowSurface), low + halfHeight, high - halfHeight);
            resident->position = Position(Fixed(px) + 0.5f, Fixed(py) + 0.5f, Fixed(z));
            if (!fitsInWater(resident->position, appearance.size,
                             [&](int bx, int by, int bz) { return cubeAt(Fixed(bx), Fixed(by), Fixed(bz)); }))
                continue;
            equip(resident->hostility, resident->race, resident->level, appearance.appearanceFlags,
                  resident->factionKind, resident->classType, resident->specialization, resident->equipment,
                  resident->specialSkills, resident->summonAppearanceMode, resident->summonRaces, resident->inventory);
            resident->angle = float((roll + school.size() * 47) % 360);
            school.push_back(std::move(resident));
            if (school.size() >= wanted)
                break;
        }
        if (school.size() >= (boss ? 1 : 2)) {
            // Residents use the world's normal streaming, death and respawn lifecycle.
            for (auto &resident : school)
                adoptResident(resident.release());
            return;
        }
    }
}

void World::AIManager::updateAquatic(Creature *creature, int milliseconds)
{
    auto &state = m_states.at(creature->m_id);
    auto &data = creature->m_entityData;
    const bool ground = bottomDweller(data.race);
    const auto valid = [&](const Position &destination) {
        return swimPath(*m_world, data.position, destination, data.appearance.size);
    };
    state.retargetDelay -= milliseconds;
    if (state.retargetDelay <= 0 && data.hostilityType == HostilityEnemy) {
        float nearest = 14.0f * 14.0f;
        Creature *prey = nullptr;
        for (auto *other : m_world->m_creatures) {
            if (other->m_entityData.hp <= 0.0f || !other->canBeDamaged() || !creature->canDoDamage(other))
                continue;
            auto position = other->m_entityData.position;
            const float distance = Size(position - data.position).squaredLength();
            if (distance < nearest && valid(position)) {
                nearest = distance;
                prey = other;
            }
        }
        if (prey)
            creature->m_aggro[prey->m_id] = std::max(1.0f, creature->m_aggro[prey->m_id]);
        state.retargetDelay = 750;
    }

    auto *target = m_world->creatureByID(creature->maxAggroTarget());
    if (target && !valid(target->m_entityData.position)) {
        creature->m_aggro.erase(target->m_id);
        target = nullptr;
    }
    Position destination = data.position;
    if (target && target->m_entityData.hp > 0.0f && data.hostilityType == HostilityEnemy) {
        combat(creature, milliseconds);
        const float reach = 1.0f + data.appearance.size[0] * 0.5f;
        if (Size(target->m_entityData.position - data.position).squaredLength() > reach * reach)
            destination = target->m_entityData.position;
    } else {
        state.idleTime -= milliseconds;
        if (state.idleTime <= 0 || Size(state.destination - data.position).squaredLength() < 0.75f) {
            state.idleTime = 1800 + rand() % 1800;
            state.destination = data.position;
            for (int attempt = 0; attempt < 5; ++attempt) {
                const float angle = (data.rotation[2] + float(rand() % 181 - 90)) * RadiansPerDegree;
                const float depth = ground ? 0.0f : float(rand() % 5 - 2) * 0.5f;
                const Position next = data.position + Position(Size(std::sin(angle) * 7, -std::cos(angle) * 7, depth));
                if (Size(next - data.origin).squaredLength() <= 32.0f * 32.0f && valid(next)) {
                    state.destination = next;
                    break;
                }
            }
        }
        if (valid(state.destination))
            destination = state.destination;
    }
    state.clearPath();
    state.targetId = 0;
    state.unsupportedStep = false;
    auto direction = Size(destination - data.position);
    if (ground)
        direction[2] = 0;
    const float distance = direction.length();
    state.acceleration = distance > 0.1f ? direction * ((target ? 13.0f : 6.0f) / distance) : Size(0, 0, 0);
}

} // namespace cube

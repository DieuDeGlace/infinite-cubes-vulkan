#include "cube/World.h"
#include "cube/CreatureIds.h"
#include "cube/Race.h"
#include "cube/Realm.h"
#include "cube/TravelParty.h"
#include "cube/WorldLayout.h"
#include "cube/ZoneIds.h"
#include <cmath>
#include <optional>
#include <random>

namespace cube {
namespace {

using BlockPoint = plasma::Vector<2, int>;
using TravelPoint = plasma::Vector<2, double>;
using GroundPoint = plasma::Vector<3, Fixed>;

constexpr size_t MaxParties = 6;
constexpr int TownSearchRadius = 7;
constexpr float TravelUpdateMs = 1000.0f;
constexpr double HideDistance = 300.0;
constexpr double ShowDistance = 220.0;
constexpr double MinimumSpawnDistance = 100.0;
constexpr double ArrivalDistance = 110.0;
constexpr double OffscreenBlocksPerSecond = 10.0;
constexpr float MaxOffscreenTimeMs = 5.0f * 60.0f * 1000.0f;
// Hostile parties keep this far beyond a village's radius: they stop at its edge and never cut through it.
constexpr float TownKeepOut = 64.0f;

TravelPoint horizontal(const Creature &creature)
{
    return {double(creature.m_entityData.position[0]), double(creature.m_entityData.position[1])};
}

double observerDistance(const TravelPoint &point, const std::vector<const Creature *> &players)
{
    double squared = HideDistance * HideDistance + 1.0;
    for (const auto *player : players)
        squared = std::min(squared, (point - horizontal(*player)).squaredLength());
    return squared;
}

struct Town {
    BlockPoint center;
    BlockPoint region;
    int realm;
    double distance;
};

std::vector<Town> nearbyTowns(World &world, const TravelPoint &point)
{
    const int cellX = floorDivide(int(std::floor(point[0])), BlocksPerPlace);
    const int cellY = floorDivide(int(std::floor(point[1])), BlocksPerPlace);
    std::vector<Town> towns;
    for (int x = cellX - TownSearchRadius; x <= cellX + TownSearchRadius; ++x) {
        for (int y = cellY - TownSearchRadius; y <= cellY + TownSearchRadius; ++y) {
            const bool large = world.isLargePlace(x, y);
            if ((large && (x % 2 || y % 2)) || world.locationType(x, y, large) != PlaceVillage)
                continue;
            const auto center = world.placeCenter(x, y, large);
            const auto region = world.findRegion(center[0], center[1]);
            towns.push_back(
                {center, region, world.mortalRealm(x, y, large), (TravelPoint(center) - point).squaredLength()});
        }
    }
    std::stable_sort(towns.begin(), towns.end(), [](const Town &a, const Town &b) { return a.distance < b.distance; });
    return towns;
}

// Standing room on the column nearest above foot: solid below and two open blocks above, within reach of it.
std::optional<GroundPoint> columnGround(World &world, int x, int y, int foot)
{
    constexpr int Reach = 24;
    for (int z = foot + Reach; z >= foot - Reach; --z) {
        if (world.getBlock(x, y, z - 1).isSolid() && !world.getBlock(x, y, z).isSolid() &&
            !world.getBlock(x, y, z + 1).isSolid())
            return GroundPoint(Fixed(x) + Fixed(0.5), Fixed(y) + Fixed(0.5), Fixed(z));
    }
    return std::nullopt;
}

// A natural ground spot in the cell at point. Without one there, the column's ground near foot, when given.
std::optional<GroundPoint> travelGround(World &world, const TravelPoint &point, std::optional<int> foot = std::nullopt)
{
    const int x = int(std::floor(point[0]));
    const int y = int(std::floor(point[1]));
    auto *zone = world.zone(floorDivide(x, BlocksPerZone), floorDivide(y, BlocksPerZone));
    const int cellX = floorDivide(x, BlocksPerCell);
    const int cellY = floorDivide(y, BlocksPerCell);
    if (!zone)
        return std::nullopt;
    if (!zone->positionCount(cellX, cellY, PositionNaturalGround))
        return foot ? columnGround(world, x, y, *foot) : std::nullopt;
    const auto spot = zone->choosePosition(cellX, cellY, PositionNaturalGround);
    const int z = spot[2].toBlock();
    const int spotX = spot[0].toBlock();
    const int spotY = spot[1].toBlock();
    if (!world.getBlock(spotX, spotY, z - 1).isSolid() || world.getBlock(spotX, spotY, z).isSolid() ||
        world.getBlock(spotX, spotY, z + 1).isSolid())
        return std::nullopt;
    return spot;
}

struct Faction {
    int kind;
    int race;
    unsigned char classType = ClassNone;
    int realm = 0;
};

Faction travelFaction(int realmId, bool hostile, unsigned int roll)
{
    Realm realm(realmId);
    if (roll % 3 == 0 && realm.hasPeople() && realm.hasTyrant() == hostile)
        return {realm.form(), realm.native(int(roll)), ClassNone, realmId};
    constexpr Faction Friendly[] = {
        {FactionOrderOfTheLight, RaceHumanMale, ClassOrderOfTheLightMember},
        {FactionDruidsOfMana, RaceElfMale, ClassDruidOfMana},
        {FactionExplorersGuild, RaceDwarfMale},
        {FactionMonsterHunters, RaceGoblinMale},
    };
    constexpr Faction Hostile[] = {
        {FactionCultOfDoom, RaceDarkCultistMale},
        {FactionSteelEmpire, RaceEmpireSoldierMale},
        {FactionObsidianKnights, RaceHumanMale, ClassObsidianKnight},
        {FactionBloodaxeClan, RaceOrcMale},
    };
    return hostile ? Hostile[roll % std::size(Hostile)] : Friendly[roll % std::size(Friendly)];
}

void placeTraveler(World &world, Creature &creature, const GroundPoint &point)
{
    auto &data = creature.m_entityData;
    data.position = point;
    data.position[2] += Fixed(data.appearance.size[2] * 0.5f);
    data.origin = data.position;
    data.region = world.findRegion(point[0].toBlock(), point[1].toBlock());
    data.velocity = {0.0f, 0.0f, 0.0f};
    data.acceleration = data.velocity;
    creature.m_travelWaypoint.reset();
}

std::optional<GroundPoint> travelWaypoint(World &world, const Creature &creature, const TravelPoint &towards,
                                          unsigned int variation, bool avoidTowns)
{
    const auto position = horizontal(creature);
    const auto difference = towards - position;
    const double distance = std::sqrt(difference.squaredLength());
    if (distance < 4.0)
        return std::nullopt;
    const auto direction = difference / distance;
    const TravelPoint side(-direction[1], direction[0]);
    constexpr int DetourOffsets[] = {-16, 16, -27, 27};
    const double foot = double(creature.m_entityData.position[2]) - creature.m_entityData.appearance.size[2] * 0.5;
    // A long stretch first, then a short one where hills or obstacles block it.
    for (const double length : {22.0, 10.0}) {
        const double step = std::min(distance, length);
        // The walker climbs about a block per block; leave room for the ground between.
        const double climb = 4.0 + 0.9 * step;
        for (size_t i = 0; i <= std::size(DetourOffsets); ++i) {
            // Keep traveling straight while the next stretch is usable. Rotate only the detour choices.
            const int offset = i == 0 ? 0 : DetourOffsets[(i - 1 + variation) % std::size(DetourOffsets)];
            const double scale = step / 22.0;
            const auto ground = travelGround(world, position + direction * step + side * (double(offset) * scale),
                                             int(std::floor(foot)));
            if (!ground || std::abs(double((*ground)[2]) - foot) > climb ||
                (TravelPoint(double((*ground)[0]), double((*ground)[1])) - position).squaredLength() <= 16.0)
                continue;
            if (avoidTowns && world.nearVillage((*ground)[0].toBlock(), (*ground)[1].toBlock(), TownKeepOut))
                continue;
            return ground;
        }
    }
    return std::nullopt;
}

}

bool World::createTravelParty(const Creature &player)
{
    const auto playerPoint = horizontal(player);
    const auto towns = nearbyTowns(*this, playerPoint);
    if (towns.size() < 2)
        return false;

    std::minstd_rand random(std::uint32_t(m_seed) ^ (std::uint32_t(m_state.m_day) * 2654435761u) ^
                            std::uint32_t(m_travelMemberSerial + 1) ^ std::uint32_t(m_clock));
    const auto &origin = towns.front();
    size_t destinationIndex = 1;
    if (random() % 3 != 0) {
        const auto crossRegion = std::find_if(towns.begin() + 1, towns.end(),
                                              [&](const Town &town) { return town.region != origin.region; });
        if (crossRegion != towns.end())
            destinationIndex = size_t(crossRegion - towns.begin());
    }
    const auto &destination = towns[destinationIndex];
    auto direction = TravelPoint(origin.center) - playerPoint;
    if (direction.squaredLength() < 1.0)
        direction = playerPoint - TravelPoint(destination.center);
    direction /= std::sqrt(direction.squaredLength());
    const TravelPoint side(-direction[1], direction[0]);
    const bool hostile = random() % 2 == 0;
    std::optional<GroundPoint> spawn;
    for (const int offset : {0, -32, 32, -64, 64}) {
        spawn = travelGround(*this, playerPoint + direction * 155.0 + side * double(offset));
        if (spawn && hostile && nearVillage((*spawn)[0].toBlock(), (*spawn)[1].toBlock(), TownKeepOut))
            spawn.reset();
        if (spawn)
            break;
    }
    if (!spawn)
        return false;

    TravelParty &party = m_travelParties.emplace_back();
    party.origin = origin.center;
    party.destination = destination.center;
    party.position = {double((*spawn)[0]), double((*spawn)[1])};
    party.hostile = hostile;
    const auto faction = travelFaction(origin.realm, hostile, unsigned(random()));
    const size_t count = 2 + random() % 3;
    const int level = int(random() % 5);
    std::set<long long> members;
    for (size_t i = 0; i < count; ++i) {
        do {
            party.members[i] = TravelPartyIdBase + m_travelMemberSerial++;
        } while (creatureByID(party.members[i]));
        members.insert(party.members[i]);
    }
    for (size_t i = 0; i < count; ++i) {
        auto creature = std::make_unique<Creature>(party.members[i]);
        auto &data = creature->m_entityData;
        data.hostilityType = hostile ? HostilityEnemy : HostilityFriendlyNpc;
        data.race = randomRace(faction.race);
        data.classType = faction.classType;
        data.factionKind = static_cast<unsigned char>(faction.kind);
        data.factionSeed = faction.realm;
        data.level = level;
        creature->m_groupMembers = members;
        fillAppearance(data.race, data.appearance, nullptr, 0);
        equip(data.hostilityType, data.race, data.level, data.appearance.appearanceFlags, data.factionKind,
              data.classType, data.specialization, data.equipment, creature->m_specialSkills,
              creature->m_summonAppearanceMode, creature->m_summonRaces, creature->m_inventory);
        auto point = *spawn;
        if (i) {
            const auto separate = travelGround(*this, party.position + side * (double(i) * 6.0));
            if (separate)
                point = *separate;
        }
        placeTraveler(*this, *creature, point);
        creature->m_pyroProgression = m_pyroProgression;
        data.hp = creature->maxHP();
        addCreature(creature.release());
    }
    return true;
}

bool World::updateTravelParty(TravelParty &party, const std::vector<const Creature *> &players, float milliseconds)
{
    Creature *leader = nullptr;
    bool survives = false;
    for (size_t i = 0; i < party.members.size(); ++i) {
        auto *member = creatureByID(party.members[i]);
        if (member && member->m_entityData.hp > 0.0f) {
            survives = true;
            if (observerDistance(horizontal(*member), players) > HideDistance * HideDistance) {
                detachCreature(member);
                party.parked[i].reset(member);
                member->m_aggro.clear();
            } else if (!leader) {
                leader = member;
            }
        }
        survives |= bool(party.parked[i]);
    }
    if (!survives)
        return false;

    if (leader) {
        party.position = horizontal(*leader);
        party.offscreenTime = 0.0f;
    } else {
        party.offscreenTime += milliseconds;
        if (party.offscreenTime >= MaxOffscreenTimeMs)
            return false;
    }
    const auto towardTown = TravelPoint(party.destination) - party.position;
    const double remaining = std::sqrt(towardTown.squaredLength());
    // A hostile party arrives at the edge of its destination instead, then turns back.
    double arrival = ArrivalDistance;
    if (party.hostile) {
        const bool large = isLargePlace(floorDivide(party.destination[0], BlocksPerPlace),
                                        floorDivide(party.destination[1], BlocksPerPlace));
        arrival = (large ? LargePlaceRadius : SmallPlaceRadius) + TownKeepOut + OffscreenBlocksPerSecond;
    }
    const bool arrived = remaining <= arrival;
    if (arrived) {
        party.departureDelay += milliseconds;
        if (party.departureDelay >= 15000.0f) {
            std::swap(party.origin, party.destination);
            party.departureDelay = 0.0f;
        }
    } else if (!leader) {
        const double step = std::min(remaining, double(milliseconds) * 0.001 * OffscreenBlocksPerSecond);
        party.position += towardTown * (step / remaining);
    }

    const double nearPlayer = observerDistance(party.position, players);
    const bool canRejoin =
        nearPlayer >= MinimumSpawnDistance * MinimumSpawnDistance && nearPlayer <= ShowDistance * ShowDistance;
    for (size_t i = 0; i < party.members.size(); ++i) {
        auto &parked = party.parked[i];
        if (parked && canRejoin) {
            const auto point = travelGround(*this, party.position + TravelPoint(double(i * 6), 0.0));
            if (point) {
                placeTraveler(*this, *parked, *point);
                addCreature(parked.release());
            }
        }
        auto *member = creatureByID(party.members[i]);
        if (!member || member->m_entityData.hp <= 0.0f)
            continue;

        const auto pos = horizontal(*member);
        auto &data = member->m_entityData;
        data.region = findRegion(int(std::floor(pos[0])), int(std::floor(pos[1])));
        const bool catchingUp = (party.position - pos).squaredLength() > 24.0 * 24.0;
        if (!catchingUp && arrived) {
            member->m_travelWaypoint.reset();
            continue;
        }
        const auto point = catchingUp ? party.position : TravelPoint(party.destination);
        const unsigned int variation = (party.waypointVariation / 5) % 5;
        member->m_travelWaypoint = travelWaypoint(*this, *member, point, variation, party.hostile);
    }
    ++party.waypointVariation;
    return true;
}

void World::updateTravelParties(float milliseconds)
{
    if (m_worldName.empty())
        return;
    m_travelPartyElapsed += milliseconds;
    if (m_travelPartyElapsed < TravelUpdateMs)
        return;
    const float elapsed = m_travelPartyElapsed;
    m_travelPartyElapsed = 0.0f;
    m_travelSpawnDelay = std::max(0.0f, m_travelSpawnDelay - elapsed);

    std::vector<const Creature *> players;
    for (const auto *creature : m_creatures) {
        const auto &data = creature->m_entityData;
        if (data.hostilityType == HostilityPlayer && data.currentAbility != AbilityMenuView && data.hp > 0.0f)
            players.push_back(creature);
    }
    m_travelParties.erase(
        std::remove_if(m_travelParties.begin(), m_travelParties.end(),
                       [&](TravelParty &party) { return !updateTravelParty(party, players, elapsed); }),
        m_travelParties.end());

    if (m_travelSpawnDelay > 0.0f || m_travelParties.size() >= MaxParties)
        return;
    for (const auto *player : players) {
        if ((player->m_entityData.flags & CreatureFlagOnGround) && createTravelParty(*player)) {
            m_travelSpawnDelay = 60000.0f;
            return;
        }
    }
    m_travelSpawnDelay = 5000.0f;
}

}

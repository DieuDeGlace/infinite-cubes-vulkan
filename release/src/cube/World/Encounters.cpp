// Planning and staging of overworld encounters (Encounter.h), adapted from DieuDeGlace's OverworldEvents (MIT). The
// mod's rules kept here: enemies are drawn from the region's own dry-land natives, a named guardian has three times the
// ordinary HP, rescue cages need flat 7 x 7 ground and containers flat 3 x 3, a key sack lies about 56 blocks from its
// chest, sites keep 192 blocks apart, objects are held at their anchors and a captive stays bound in its cage.
#include "cube/World.h"
#include "cube/CreatureIds.h"
#include "cube/MissionIds.h"
#include "cube/Missions.h"
#include "cube/Race.h"
#include "cube/TravelParty.h"
#include "cube/WorldLayout.h"
#include <cmath>
#include <cstdio>
#include <random>

namespace cube {
namespace {

using Point = plasma::Vector<3, Fixed>;

constexpr float UpdateMs = 1000.0f;
constexpr int RandomCap = 2;             // unfinished encounters found on the player's own at once
constexpr float PlanIntervalMs = 120000; // between encounters found on the player's own
constexpr double PlanNearest = 160.0, PlanFarthest = 400.0;
constexpr double SiteSpacing = 450.0;     // between any two encounter sites
constexpr double StageDistance = 200.0;   // actors exist within this
constexpr double UnstageDistance = 320.0; // and leave beyond this
constexpr double ForgetDistance = 900.0;  // an encounter nobody asked for is dropped beyond this
constexpr float TownKeepOut = 64.0f;
constexpr double SackDistance = 56.0;
constexpr float GuardianHpScale = 3.0f;
constexpr float GuardianSize = 1.3f;
constexpr float ProtectedHpScale = 0.0f; // marks bounty givers, who take no damage (Creature::setProtected)
constexpr double EnemySampleRange = 400.0;
constexpr double LandmarkReach = 160.0; // a landmark group gathers as the player comes this close
constexpr int LandmarkCap = 2;          // unfinished landmark groups at once
constexpr int CenterpieceReach = 34;    // a group is placed beside a centerpiece (a mana tree's trunk) up to this far

// Enemy spots around the centre, as in the mod's role offsets; a lone guardian stands at the centre.
constexpr std::array<std::array<int, 2>, Encounter::MaxEnemies> EnemyOffsets = {
    {{-8, -7}, {8, -7}, {-10, 5}, {10, 5}, {-5, 12}, {5, 12}}};

struct Faction {
    int kind;
    int race;
    unsigned char classType;
};
constexpr Faction Adventurers[] = {
    {FactionOrderOfTheLight, RaceHumanMale, ClassOrderOfTheLightMember},
    {FactionDruidsOfMana, RaceElfMale, ClassDruidOfMana},
    {FactionExplorersGuild, RaceDwarfMale, ClassNone},
    {FactionMonsterHunters, RaceGoblinMale, ClassNone},
};
constexpr int Bandits[] = {RaceDarkCultistMale, RaceEmpireSoldierMale, RaceOrcMale};

std::array<int, 3> blocksOf(const Point &point)
{
    return {point[0].toBlock(), point[1].toBlock(), point[2].toBlock()};
}

double horizontal(const std::array<int, 3> &a, const Point &b)
{
    const double dx = double(a[0]) + 0.5 - double(b[0]), dy = double(a[1]) + 0.5 - double(b[1]);
    return std::sqrt(dx * dx + dy * dy);
}

// Standing room on the column nearest below around + 40: solid underfoot, open air (not water) above.
bool groundAt(World &world, int x, int y, int around, int &z)
{
    if (!world.zone(floorDivide(x, BlocksPerZone), floorDivide(y, BlocksPerZone)))
        return false;
    for (int top = around + 40; top >= around - 60; --top) {
        const Block below = world.getBlock(x, y, top - 1);
        if (!below.isSolid())
            continue;
        const Block at = world.getBlock(x, y, top), above = world.getBlock(x, y, top + 1);
        if (at.isSolid() || above.isSolid())
            return false;
        if (at.type == BlockWater || below.type == BlockWater || top <= 1)
            return false;
        z = top;
        return true;
    }
    return false;
}

// The host only loads the zones within 270 blocks of a player. Elsewhere the terrain's height stands in for the
// ground: z is the first open block above it, and water rules the spot out.
bool terrainGround(World &world, int x, int y, int &z)
{
    z = int(std::floor(world.terrainHeight(x, y, 0))) + 1;
    return z > 1 && !world.hasSurfaceWater(x, y, z - 1, z + 1);
}

// The same for a flat footprint: its centre and corners at one height.
bool terrainFlat(World &world, int x, int y, int radius, int &z)
{
    if (!terrainGround(world, x, y, z))
        return false;
    for (const int dx : {-radius, radius}) {
        for (const int dy : {-radius, radius}) {
            int corner;
            if (!terrainGround(world, x + dx, y + dy, corner) || corner != z)
                return false;
        }
    }
    return true;
}

// Flat ground of (2 radius + 1) squared columns at one height.
bool flatFootprint(World &world, int x, int y, int around, int radius, int &z)
{
    if (!groundAt(world, x, y, around, z))
        return false;
    for (int dx = -radius; dx <= radius; ++dx) {
        for (int dy = -radius; dy <= radius; ++dy) {
            int other;
            if ((dx || dy) && (!groundAt(world, x + dx, y + dy, z, other) || other != z))
                return false;
        }
    }
    return true;
}

} // namespace

Encounter *World::encounterOf(long long actorId, int *role)
{
    if (!actorId)
        return nullptr;
    for (Encounter &encounter : m_encounters) {
        for (int i = 0; i < Encounter::RoleCount; ++i) {
            if (encounter.actors[i] == actorId) {
                if (role)
                    *role = i;
                return &encounter;
            }
        }
    }
    return nullptr;
}

Encounter *World::startEncounter(Encounter::Kind kind, const Creature &player, double nearest, double farthest,
                                 const std::array<int, 2> *around)
{
    std::minstd_rand random(std::uint32_t(m_seed) ^ std::uint32_t(m_clock) ^ (m_encounterSerial + 1) * 2654435761u);
    const Point from = player.m_entityData.position;
    const int footprint = kind == Encounter::Rescue ? 3 : 1;
    const bool flat = kind == Encounter::Rescue || kind == Encounter::ChestSack || kind == Encounter::ChestBoss;
    bool grounded = true; // every site chosen stands on loaded ground
    const auto spaced = [&](int x, int y) {
        for (const Encounter &other : m_encounters) {
            const double dx = double(x - other.site[0]), dy = double(y - other.site[1]);
            if (dx * dx + dy * dy < SiteSpacing * SiteSpacing)
                return false;
        }
        return !nearVillage(x, y, TownKeepOut);
    };
    // height is where to look for the ground; without one, the terrain's own height at each spot.
    const auto siteNear = [&](double cx, double cy, const int *height, double low, double high, int radius,
                              std::array<int, 3> &site) {
        const double start = double(random() % 360);
        for (int attempt = 0; attempt < 12; ++attempt) {
            const double angle = (start + attempt * 30.0) * 0.017453292519943295;
            const double distance = low + double(random() % 1000) / 1000.0 * (high - low);
            const int x = int(std::floor(cx + std::cos(angle) * distance));
            const int y = int(std::floor(cy + std::sin(angle) * distance));
            const int hint = height ? *height : int(std::floor(terrainHeight(x, y, 0)));
            const bool loaded = zone(floorDivide(x, BlocksPerZone), floorDivide(y, BlocksPerZone)) != nullptr;
            int z;
            const bool ground =
                loaded ? (flat ? flatFootprint(*this, x, y, hint, radius, z) : groundAt(*this, x, y, hint, z))
                       : (flat ? terrainFlat(*this, x, y, radius, z) : terrainGround(*this, x, y, z));
            if (!ground)
                continue;
            grounded &= loaded;
            site = {x, y, z};
            return true;
        }
        return false;
    };

    Encounter encounter;
    encounter.kind = kind;
    encounter.worldSeed = m_seed;
    encounter.roll = std::uint32_t(random());
    std::array<int, 3> site;
    const double originX = around ? (*around)[0] + 0.5 : double(from[0]);
    const double originY = around ? (*around)[1] + 0.5 : double(from[1]);
    bool found = false;
    for (int attempt = 0; attempt < 3 && !found; ++attempt) {
        found = siteNear(originX, originY, nullptr, nearest, farthest, footprint, site) && spaced(site[0], site[1]);
    }
    if (!found)
        return nullptr;
    encounter.site = site;
    if (kind == Encounter::ChestSack && !siteNear(site[0] + 0.5, site[1] + 0.5, &site[2], SackDistance - 4.0,
                                                  SackDistance + 4.0, 1, encounter.sackSite))
        return nullptr;

    encounter.grounded = grounded;
    chooseEncounterEnemies(encounter, player);
    encounter.serial = ++m_encounterSerial;
    m_encounters.push_back(encounter);
    return &m_encounters.back();
}

void World::chooseEncounterEnemies(Encounter &encounter, const Creature &player)
{
    // The region's own enemies: dry-land natives near the site, skipping fish, bosses and other encounters' actors.
    std::vector<const Creature *> pool;
    for (const Creature *creature : m_creatures) {
        const auto &data = creature->m_entityData;
        if (data.hostilityType != HostilityEnemy || data.hp <= 0.0f || isTravelPartyMember(creature->m_id) ||
            (data.appearance.appearanceFlags &
             (AppearanceFlagAquatic | AppearanceFlagLarge | AppearanceFlagTinted | AppearanceFlagFlying)) ||
            horizontal(encounter.site, data.position) > EnemySampleRange)
            continue;
        pool.push_back(creature);
    }
    encounter.enemyCount = Encounter::enemiesFor(encounter.kind, encounter.roll);
    encounter.progressionLevel = player.m_entityData.progressionLevel;
    if (pool.empty()) {
        for (int i = 0; i < encounter.enemyCount; ++i)
            encounter.races[i] = randomRace(Bandits[(encounter.roll >> (4 + i)) % std::size(Bandits)]);
        encounter.level = 2;
        return;
    }
    // The strongest native nearby leads a guardian's or landmark's group; a battle fields one kind of creature;
    // a landmark's followers are any of the natives.
    const Creature *strongest = pool.front();
    for (const Creature *candidate : pool) {
        if (candidate->m_entityData.level > strongest->m_entityData.level)
            strongest = candidate;
    }
    const Creature *common = pool[(encounter.roll >> 8) % pool.size()];
    for (int i = 0; i < encounter.enemyCount; ++i) {
        const Creature *sample = encounter.kind == Encounter::Battle ? common
                                 : i == 0                            ? strongest
                                 : encounter.isLandmark() ? pool[(encounter.roll >> (8 + 3 * i)) % pool.size()]
                                                          : strongest;
        encounter.races[i] = sample->m_entityData.race;
    }
    const Creature &lead = encounter.kind == Encounter::Battle ? *common : *strongest;
    encounter.level = std::uint8_t(std::clamp<int>(lead.m_entityData.level, 0, 4));
    encounter.progressionLevel = lead.m_entityData.progressionLevel;
}

Encounter *World::startLandmark(Encounter::Kind kind, const std::array<int, 2> &cell, const Creature &player)
{
    const bool large = isLargePlace(cell[0], cell[1]);
    const auto center = placeCenter(cell[0], cell[1], large);
    const int surface = int(std::floor(terrainHeight(center[0], center[1], 0)));
    // The centre, or open ground beside a centerpiece standing on it.
    std::array<int, 3> site;
    bool found = false;
    for (int reach = 0; reach <= CenterpieceReach && !found; reach += 8) {
        for (int step = 0; step < (reach ? 8 : 1) && !found; ++step) {
            const double angle = step * 0.7853981633974483;
            const int x = center[0] + int(std::lround(std::cos(angle) * reach));
            const int y = center[1] + int(std::lround(std::sin(angle) * reach));
            int z;
            if (groundAt(*this, x, y, surface, z) && std::abs(z - surface) <= 4) {
                site = {x, y, z};
                found = true;
            }
        }
    }
    if (!found)
        return nullptr;
    Encounter encounter;
    encounter.kind = kind;
    encounter.worldSeed = m_seed;
    encounter.roll = std::uint32_t(std::minstd_rand(std::uint32_t(m_seed) ^ std::uint32_t(cell[0]) * 73856093u ^
                                                    std::uint32_t(cell[1]) * 19349663u ^ std::uint32_t(m_clock))());
    encounter.site = site;
    encounter.grounded = true;
    encounter.place = cell;
    chooseEncounterEnemies(encounter, player);
    encounter.serial = ++m_encounterSerial;
    m_encounters.push_back(encounter);
    return &m_encounters.back();
}

void World::stageEncounter(Encounter &encounter)
{
    if (!encounter.grounded) {
        // The sites came from the terrain's height: stand them on the loaded ground (a tree or a house may be there).
        for (auto *spot : {&encounter.site, &encounter.sackSite}) {
            int z;
            const bool used = (*spot)[0] != 0 || (*spot)[1] != 0;
            if (used && groundAt(*this, (*spot)[0], (*spot)[1], (*spot)[2], z))
                (*spot)[2] = z;
        }
        encounter.grounded = true;
    }
    const auto present = [&](long long id) {
        const Creature *creature = creatureByID(id);
        return creature && creature->m_entityData.hp > 0.0f;
    };
    // The difficulty premium (OverworldEvents): the player's level or the region's, whichever is higher.
    const Creature *player = encounterPlayer();
    const int playerLevel = player ? player->displayLevel() : 1;
    const int regionLevel = m_pyroProgression ? encounter.progressionLevel : 1 + 8 * encounter.level;
    const int difficulty = std::clamp(std::max(playerLevel, regionLevel), 1, 10000000);
    const auto spawn = [&](int role, unsigned char hostility, int race, int level, const std::array<int, 3> &at,
                           float size, float hpScale, const Faction &faction) {
        long long id;
        do {
            id = TravelPartyIdBase + m_travelMemberSerial++;
        } while (creatureByID(id));
        auto creature = std::make_unique<Creature>(id);
        auto &data = creature->m_entityData;
        data.hostilityType = hostility;
        data.race = race;
        data.level = level;
        data.classType = faction.classType;
        data.factionKind = static_cast<unsigned char>(faction.kind);
        fillAppearance(data.race, data.appearance, nullptr, 0);
        if (size != 1.0f) {
            auto &appearance = data.appearance;
            appearance.size *= size;
            appearance.headScale *= size;
            appearance.chestScale *= size;
            appearance.feetScale *= size;
            appearance.handsScale *= size;
            appearance.tailScale *= size;
        }
        equip(data.hostilityType, data.race, data.level, data.appearance.appearanceFlags, data.factionKind,
              data.classType, data.specialization, data.equipment, creature->m_specialSkills,
              creature->m_summonAppearanceMode, creature->m_summonRaces, creature->m_inventory);
        data.position = Point(Fixed(at[0]) + Fixed(0.5), Fixed(at[1]) + Fixed(0.5),
                              Fixed(at[2]) + Fixed(data.appearance.size[2] * 0.5f));
        data.origin = data.position;
        data.region = findRegion(at[0], at[1]);
        data.progressionLevel = hostility == HostilityEnemy ? encounter.progressionLevel : 1;
        if (hostility == HostilityEnemy) {
            creature->setHpScale(hpScale * Encounter::healthPremium(difficulty));
            creature->m_attackScale = Encounter::damagePremium(difficulty);
        } else if (hpScale == ProtectedHpScale) {
            creature->setProtected();
        } else {
            creature->setHpScale(hpScale);
        }
        creature->m_pyroProgression = m_pyroProgression;
        data.hp = creature->maxHP();
        encounter.actors[role] = id;
        addCreature(creature.release());
    };
    const auto offset = [&](int dx, int dy) {
        std::array<int, 3> at = encounter.site;
        int z;
        if (groundAt(*this, at[0] + dx, at[1] + dy, at[2], z))
            at = {at[0] + dx, at[1] + dy, z};
        return at;
    };
    const Faction none {0, 0, ClassNone};
    const Faction foes {0, 0, ClassNone};
    const Faction &friends = Adventurers[(encounter.roll >> 12) % std::size(Adventurers)];

    for (int i = 0; i < encounter.enemyCount; ++i) {
        if ((encounter.defeated >> i) & 1 || present(encounter.actors[i]))
            continue;
        const bool guardian = i == 0 && encounter.hasGuardian();
        // A lone guardian, and a landmark group's leader, hold the centre.
        const auto at = encounter.enemyCount == 1 || (i == 0 && encounter.isLandmark())
                            ? encounter.site
                            : offset(EnemyOffsets[i][0], EnemyOffsets[i][1]);
        spawn(Encounter::FirstEnemy + i, HostilityEnemy, encounter.races[i],
              guardian ? std::min(4, encounter.level + 1) : encounter.level, at, guardian ? GuardianSize : 1.0f,
              guardian ? GuardianHpScale : 1.0f, foes);
        if (guardian) {
            // The mod's named boss: its nameplate carries a name drawn from the encounter's roll.
            if (Creature *boss = creatureByID(encounter.actors[Encounter::FirstEnemy]))
                std::snprintf(boss->m_entityData.name, sizeof(boss->m_entityData.name), "%s",
                              encounter.bossName().c_str());
        }
    }
    switch (encounter.kind) {
    case Encounter::Battle:
        if (!encounter.failed) {
            if (!present(encounter.actors[Encounter::Npc]))
                spawn(Encounter::Npc, HostilityFriendlyNpc, randomRace(friends.race), encounter.level, offset(-3, 0),
                      1.0f, 1.0f, friends);
            if (!present(encounter.actors[Encounter::Companion]))
                spawn(Encounter::Companion, HostilityFriendlyNpc, randomRace(friends.race), encounter.level,
                      offset(3, 0), 1.0f, 1.0f, friends);
        }
        break;
    case Encounter::Bounty:
        // The adventurer waits beyond the guardian's notice.
        if (!present(encounter.actors[Encounter::Npc]))
            spawn(Encounter::Npc, HostilityFriendlyNpc, randomRace(friends.race), encounter.level, offset(-24, 0), 1.0f,
                  ProtectedHpScale, friends);
        break;
    case Encounter::Rescue:
        if (!present(encounter.actors[Encounter::Object])) {
            spawn(Encounter::Object, HostilityInteractiveObject, RaceCage1 + int(encounter.roll % 4), 0, encounter.site,
                  1.0f, 1.0f, none);
            if (Creature *cage = creatureByID(encounter.actors[Encounter::Object]))
                cage->m_entityData.interactionState = encounter.opened ? 1 : 0;
        }
        // The captive has normal health: while caged it is held like a gnome in a cauldron, so enemies ignore it.
        if (!encounter.rewarded && !present(encounter.actors[Encounter::Npc]))
            spawn(Encounter::Npc, HostilityFriendlyNpc, randomRace(friends.race), encounter.level, encounter.site, 1.0f,
                  1.0f, friends);
        break;
    case Encounter::Hill:
    case Encounter::Camp:
    case Encounter::Shrine:
        // When the whole group has fallen, an unlocked reward chest stands at the centre.
        if (encounter.enemiesDefeated() && !present(encounter.actors[Encounter::Container])) {
            spawn(Encounter::Container, HostilityInteractiveObject, RaceWoodChest, 0, encounter.site, 1.0f, 1.0f, none);
            if (Creature *chest = creatureByID(encounter.actors[Encounter::Container]))
                chest->m_entityData.interactionState = encounter.opened ? 2 : 0;
        }
        break;
    case Encounter::ChestSack:
        if (!encounter.keyGranted && !present(encounter.actors[Encounter::Object]))
            spawn(Encounter::Object, HostilityInteractiveObject, RaceSack, 0, encounter.sackSite, 1.0f, 1.0f, none);
        [[fallthrough]];
    case Encounter::ChestBoss:
        if (!present(encounter.actors[Encounter::Container])) {
            spawn(Encounter::Container, HostilityInteractiveObject, RaceWoodChest, 0, encounter.site, 1.0f, 1.0f, none);
            if (Creature *chest = creatureByID(encounter.actors[Encounter::Container]))
                chest->m_entityData.interactionState = encounter.opened ? 2 : 0;
        }
        break;
    default:
        break;
    }
}

void World::unstageEncounter(Encounter &encounter)
{
    for (long long &id : encounter.actors) {
        if (id)
            removeCreature(creatureByID(id));
        id = 0;
    }
}

Creature *World::encounterPlayer()
{
    if (m_localCreature)
        return m_localCreature;
    for (Creature *creature : m_creatures) {
        if (creature->m_entityData.hostilityType == HostilityPlayer)
            return creature;
    }
    return nullptr;
}

void World::importEncounters(const std::vector<Encounter> &saved)
{
    m_encountersImported = true;
    for (const Encounter &record : saved) {
        if (record.worldSeed != m_seed || record.finished())
            continue;
        Encounter encounter = record;
        encounter.actors = {};
        encounter.serial = ++m_encounterSerial;
        m_encounters.push_back(encounter);
    }
}

void World::updateEncounters(float milliseconds)
{
    // The session waits for the character's accepted encounters (Game::updateEncounterQuests imports them).
    Creature *character = encounterPlayer();
    if (m_isClient || m_worldName.empty() || !m_encountersImported || !character)
        return;

    // Hold the objects at their anchors and the captive in its cage every frame, so nothing pushes them about.
    for (Encounter &encounter : m_encounters) {
        for (const int role : {int(Encounter::Object), int(Encounter::Container)}) {
            Creature *object = creatureByID(encounter.actors[role]);
            if (!object)
                continue;
            const auto &at = role == Encounter::Object && encounter.kind == Encounter::ChestSack ? encounter.sackSite
                                                                                                 : encounter.site;
            const Point anchor(Fixed(at[0]) + Fixed(0.5), Fixed(at[1]) + Fixed(0.5),
                               Fixed(at[2]) + Fixed(object->m_entityData.appearance.size[2] * 0.5f));
            object->m_entityData.position = anchor;
            object->m_drawnPosition = anchor;
            object->m_entityData.velocity = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
            object->m_entityData.acceleration = object->m_entityData.velocity;
        }
        Creature *cage =
            encounter.kind == Encounter::Rescue ? creatureByID(encounter.actors[Encounter::Object]) : nullptr;
        Creature *captive = cage ? creatureByID(encounter.actors[Encounter::Npc]) : nullptr;
        if (captive && !encounter.opened) {
            // The native cages' seat: 0.03 blocks in from the centre and resting on the cage's floor.
            const Point seat(plasma::Vector<3, float>(0.03f, 0.03f,
                                                      0.03f - cage->m_entityData.appearance.size[2] * 0.5f +
                                                          captive->m_entityData.appearance.size[2] * 0.5f));
            captive->m_entityData.position = cage->m_entityData.position + seat;
            captive->m_drawnPosition = cage->m_drawnPosition + seat;
            captive->m_entityData.velocity = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
            captive->m_entityData.acceleration = captive->m_entityData.velocity;
            captive->m_entityData.currentAbility = AbilitySit;
            // Held like a cauldron's or cage's occupant: areEnemies and the AI skip an active creature, and m_caged
            // keeps it active through World::update's reset. Hits that reach it anyway do not lower its health.
            captive->m_caged = true;
            captive->m_entityData.binaryToggles.active = 1;
            if (!captive->isProtected())
                captive->setProtected();
        } else if (captive && captive->m_caged) {
            // Released: enemies may target and hurt it again, as with an occupant leaving a cauldron.
            captive->m_caged = false;
            captive->m_entityData.binaryToggles.active = 0;
            captive->m_entityData.currentAbility = AbilityNone;
            captive->setHpScale(1.0f);
        }
    }

    m_encounterElapsed += milliseconds;
    if (m_encounterElapsed < UpdateMs)
        return;
    const float elapsed = m_encounterElapsed;
    m_encounterElapsed = 0.0f;
    const Point player = character->m_entityData.position;

    for (Encounter &encounter : m_encounters) {
        // A staged enemy that is gone or down was defeated: staging removals clear the ID first.
        for (int i = 0; i < encounter.enemyCount; ++i) {
            const long long id = encounter.actors[i];
            const Creature *enemy = id ? creatureByID(id) : nullptr;
            if (id && (!enemy || enemy->m_entityData.hp <= 0.0f)) {
                encounter.defeated |= std::uint8_t(1u << i);
                encounter.actors[i] = 0;
            }
        }
        if (encounter.kind == Encounter::Battle && !encounter.finished() && !encounter.enemiesDefeated()) {
            bool standing = false;
            for (const int role : {int(Encounter::Npc), int(Encounter::Companion)}) {
                const long long id = encounter.actors[role];
                const Creature *ally = id ? creatureByID(id) : nullptr;
                standing |= !id || (ally && ally->m_entityData.hp > 0.0f);
            }
            encounter.failed = !standing;
        }
        const double distance = horizontal(encounter.site, player);
        if (distance <= StageDistance)
            stageEncounter(encounter);
        else if (distance > UnstageDistance)
            unstageEncounter(encounter);
    }
    // Encounters left far behind: the finished ones and those nobody asked the player to finish. A landmark group
    // keeps its instance until it is finished, and then stays done for the session.
    for (const Encounter &encounter : m_encounters) {
        if (encounter.isLandmark() && encounter.finished())
            m_landmarksDone.insert(encounter.place);
    }
    m_encounters.erase(std::remove_if(m_encounters.begin(), m_encounters.end(),
                                      [&](Encounter &encounter) {
                                          const bool open = encounter.isLandmark()
                                                                ? !encounter.finished()
                                                                : encounter.accepted && !encounter.finished();
                                          const bool drop =
                                              !open && horizontal(encounter.site, player) > ForgetDistance;
                                          if (drop)
                                              unstageEncounter(encounter);
                                          return drop;
                                      }),
                       m_encounters.end());

    // The native places around the player hold their landmark groups.
    int landmarks = 0;
    for (const Encounter &encounter : m_encounters)
        landmarks += encounter.isLandmark() && !encounter.finished();
    const int placeX = floorDivide(player[0].toBlock(), BlocksPerPlace);
    const int placeY = floorDivide(player[1].toBlock(), BlocksPerPlace);
    for (int i = placeX - 1; i <= placeX + 1 && landmarks < LandmarkCap; ++i) {
        for (int j = placeY - 1; j <= placeY + 1 && landmarks < LandmarkCap; ++j) {
            const bool large = isLargePlace(i, j);
            const std::array<int, 2> cell =
                large ? std::array<int, 2> {floorDivide(i, 2) * 2, floorDivide(j, 2) * 2} : std::array<int, 2> {i, j};
            Encounter::Kind kind;
            if (!Encounter::landmarkKind(locationType(cell[0], cell[1], large), kind) || m_landmarksDone.count(cell) ||
                std::any_of(m_encounters.begin(), m_encounters.end(), [&](const Encounter &encounter) {
                    return encounter.isLandmark() && encounter.place == cell;
                }))
                continue;
            const auto center = placeCenter(cell[0], cell[1], large);
            const double dx = double(center[0]) - double(player[0]), dy = double(center[1]) - double(player[1]);
            if (dx * dx + dy * dy > LandmarkReach * LandmarkReach)
                continue;
            // A place where the region's missions keep a utility item (a glider, a boat, the reins...) keeps it alone.
            auto holds = m_landmarkHoldsUtility.find(cell);
            if (holds == m_landmarkHoldsUtility.end()) {
                const int mission = Missions(this, findRegion(center[0], center[1])).kindAt(cell[0], cell[1]);
                holds = m_landmarkHoldsUtility
                            .emplace(cell, mission >= MissionKindHangGliderPlacement &&
                                               mission <= MissionKindTreasureSpiritPlacement)
                            .first;
            }
            if (holds->second)
                continue;
            landmarks += startLandmark(kind, cell, *character) != nullptr;
        }
    }

    // Now and then an encounter turns up on its own nearby, marked on the map.
    m_encounterPlanDelay -= elapsed;
    if (m_encounterPlanDelay > 0.0f || !(character->m_entityData.flags & CreatureFlagOnGround))
        return;
    int open = 0;
    for (const Encounter &encounter : m_encounters)
        open += !encounter.finished() && !encounter.accepted && !encounter.isLandmark();
    if (open >= RandomCap) {
        m_encounterPlanDelay = 5000.0f;
        return;
    }
    const auto kind = static_cast<Encounter::Kind>((m_encounterSerial * 2654435761u >> 7) % Encounter::RandomKindCount);
    m_encounterPlanDelay = startEncounter(kind, *character, PlanNearest, PlanFarthest) ? PlanIntervalMs : 5000.0f;
}

} // namespace cube

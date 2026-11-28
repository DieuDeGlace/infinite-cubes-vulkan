// The player's side of the overworld encounters (Encounter.h; World/Encounters.cpp), adapted from DieuDeGlace's
// OverworldEvents (MIT): friendly travelers point the player to one, its adventurers and captive talk, its cage, sack
// and chest open, and rewards are paid. Chests open with the native Rusty Key (quest item 6).
#include "cube/Game.h"
#include "cube/CreatureIds.h"
#include "cube/TravelParty.h"
#include "cube/WorldLayout.h"
#include "cube/widgets/ChatWidget.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <random>

namespace cube {
namespace {

constexpr int ReferralCap = 2; // accepted, unfinished encounters at once
constexpr double ReferralNearest = 110.0, ReferralFarthest = 250.0;
constexpr double ReferralReuse = 600.0; // an encounter already found this close is referred instead of a new one
// A village's request lies this far beyond its edge, which is kept clear by TownKeepOut in World/Encounters.cpp.
constexpr double VillageNearest = 96.0, VillageFarthest = 320.0;
constexpr int GoldPerStar = 25;
const plasma::Vector<4, float> EncounterColor(1.0f, 0.84f, 0.40f, 1.0f);

using Point = plasma::Vector<3, Fixed>;

double distanceTo(const std::array<int, 3> &site, const Point &from)
{
    const double dx = double(site[0]) + 0.5 - double(from[0]), dy = double(site[1]) + 0.5 - double(from[1]);
    return std::sqrt(dx * dx + dy * dy);
}

std::wstring direction(const std::array<int, 3> &site, const Point &from)
{
    constexpr const wchar_t *Directions[] = {L"east", L"northeast", L"north", L"northwest",
                                             L"west", L"southwest", L"south", L"southeast"};
    const double x = double(site[0]) + 0.5 - double(from[0]), y = double(site[1]) + 0.5 - double(from[1]);
    const int octant = (int(std::lround(std::atan2(y, x) * 1.2732395447351627)) + 8) % 8;
    return std::to_wstring(int(std::sqrt(x * x + y * y))) + L" blocks " + Directions[octant];
}

void line(std::vector<Dialog> &dialogs, const std::wstring &text)
{
    writeDialogs(dialogs, text, EncounterColor);
}

// Where the player's next step is: the sack until its key is found, otherwise the centre.
const std::array<int, 3> &goal(const Encounter &encounter)
{
    return encounter.kind == Encounter::ChestSack && !encounter.keyGranted ? encounter.sackSite : encounter.site;
}

std::wstring boss(const Encounter &encounter)
{
    const std::string name = encounter.bossName();
    return std::wstring(name.begin(), name.end());
}

std::wstring objective(const Encounter &encounter)
{
    switch (encounter.kind) {
    case Encounter::Battle:
        return encounter.enemiesDefeated() ? L"Speak to the adventurers" : L"Help the adventurers win their fight";
    case Encounter::Rescue:
        return encounter.enemiesDefeated() ? L"Open the cage"
                                           : L"Defeat " + boss(encounter) + L", who holds the captive";
    case Encounter::Bounty:
        return encounter.enemiesDefeated() ? L"Return to the adventurer" : L"Defeat " + boss(encounter);
    case Encounter::ChestSack:
        return encounter.keyGranted ? L"Unlock the chest" : L"Find the key in a sack near the chest";
    case Encounter::ChestBoss:
        return encounter.keyGranted ? L"Unlock the chest"
                                    : L"Defeat " + boss(encounter) + L", who holds the chest's key";
    case Encounter::Hill:
    case Encounter::Camp:
    case Encounter::Shrine:
        return encounter.enemiesDefeated() ? L"Open the reward chest"
                                           : L"Defeat " + boss(encounter) + L" and the group";
    default:
        return {};
    }
}

// The mod's offer lines, told by a traveler on the road.
std::wstring offer(const Encounter &encounter, const std::wstring &where)
{
    switch (encounter.kind) {
    case Encounter::Battle:
        return L"Adventurers are fighting for their lives " + where +
               L". Lend them your blade! I marked it on your map.";
    case Encounter::Rescue:
        return L"One of ours was dragged off and locked in a cage " + where +
               L". Defeat the guard and set them free. It's on your map.";
    case Encounter::Bounty:
        return L"A fearsome guardian called " + boss(encounter) + L" prowls " + where +
               L". An adventurer waits nearby for someone brave enough to bring it down.";
    case Encounter::ChestSack:
        return L"There is a locked chest " + where + L". Its key was hidden in a sack about 56 blocks from it.";
    case Encounter::ChestBoss:
        return boss(encounter) + L" keeps the key to a chest " + where + L". Defeat it and the chest is yours.";
    default:
        return {};
    }
}

Item rustyKey(const Encounter &encounter)
{
    // Bound to its encounter, as the mod binds each key to its event.
    Item key;
    key.category = ItemCategoryQuest;
    key.id = QuestItemRustyKey;
    key.modifier = int(encounter.roll & 0x7fffffff);
    return key;
}

// The centre of the village a villager stands in, in blocks.
bool villageCenter(World &world, const Creature &villager, std::array<int, 2> &center)
{
    World::AreaInfo area;
    world.fillAreaInfo(villager.m_entityData.position[0].toBlock(), villager.m_entityData.position[1].toBlock(), area);
    if (area.type != PlaceVillage)
        return false;
    center = {area.center[0], area.center[1]};
    return true;
}

// The radius of that village, in blocks.
int villageRadius(World &world, const Creature &villager)
{
    World::AreaInfo area;
    world.fillAreaInfo(villager.m_entityData.position[0].toBlock(), villager.m_entityData.position[1].toBlock(), area);
    return area.large ? LargePlaceRadius : SmallPlaceRadius;
}

// The accepted request this village gave, if one is open.
Encounter *villageRequest(World &world, const std::array<int, 2> &town)
{
    for (Encounter &encounter : world.m_encounters) {
        if (encounter.fromTown && encounter.accepted && !encounter.rewarded && encounter.town == town &&
            encounter.worldSeed == world.m_seed)
            return &encounter;
    }
    return nullptr;
}

int acceptedEncounters(const World &world)
{
    int accepted = 0;
    for (const Encounter &encounter : world.m_encounters)
        accepted += encounter.accepted && !encounter.finished();
    return accepted;
}

// Holds the host's world while the game reads or changes its encounters.
class HostLock {
public:
    explicit HostLock(World &world)
        : m_world(world)
    {
        m_world.enterZoneMeshes();
    }
    ~HostLock() { m_world.leaveZoneMeshes(); }
    HostLock(const HostLock &) = delete;
    HostLock &operator=(const HostLock &) = delete;

private:
    World &m_world;
};

// The mod's village lines: the request, the reminder and the thanks.
std::wstring villageOffer(const Encounter &encounter, const std::wstring &where)
{
    return encounter.kind == Encounter::Rescue
               ? L"Bandits took one of our people and locked them in a cage " + where +
                     L". Please, free them! Come back to me afterwards and the village will reward you."
               : L"Travelers from our village were ambushed " + where +
                     L". Help them drive off the attackers, then return here for your reward.";
}

} // namespace

bool canReferVillageEncounter(World &world, const Creature &creature)
{
    std::array<int, 2> town {};
    if (!world.m_encountersLive || std::uint64_t(creature.m_id) % 3 != 1 || !isTownVillager(world, creature) ||
        !villageCenter(world, creature, town))
        return false;
    const Encounter *open = villageRequest(world, town);
    return open ? open->siteDone() && !open->failed : acceptedEncounters(world) < ReferralCap;
}

bool owesEncounterReward(World &world, const Creature &creature)
{
    // Cheap first: most frames no village is owed anything.
    if (std::none_of(world.m_encounters.begin(), world.m_encounters.end(), [](const Encounter &encounter) {
            return encounter.fromTown && encounter.accepted && !encounter.finished() && encounter.siteDone();
        }))
        return false;
    std::array<int, 2> town {};
    if (!world.m_encountersLive || std::uint64_t(creature.m_id) % 3 != 1 || !isTownVillager(world, creature) ||
        !villageCenter(world, creature, town))
        return false;
    const Encounter *open = villageRequest(world, town);
    return open && open->siteDone() && !open->failed;
}

bool canReferEncounter(World &world, const Creature &creature)
{
    const auto &data = creature.m_entityData;
    if (!world.m_encountersLive || !isTravelPartyMember(creature.m_id) || creature.m_id % 2 != 0 ||
        data.hostilityType != HostilityFriendlyNpc || data.hp <= 0.0f || world.isEncounterActor(creature.m_id))
        return false;
    return acceptedEncounters(world) < ReferralCap;
}

World *Game::encounterHost()
{
    return m_client.isConnected() ? nullptr : &m_host.m_world;
}

void Game::mirrorEncounters(World &host)
{
    m_world->m_encounters = host.m_encounters;
    m_world->m_encountersImported = host.m_encountersImported;
}

void Game::saveEncounterProgress()
{
    if (World *host = encounterHost()) {
        const HostLock hostLock(*host);
        mirrorEncounters(*host);
    }
    saveCharacter();
}

bool Game::talkAboutEncounter(Creature &speaker, std::vector<Dialog> &dialogs)
{
    Creature &player = *m_world->getLocalCreature();
    World *host = encounterHost();
    if (!player.m_playerData || m_currentCharacterSlot < 0 || !host || !m_world->m_encountersLive)
        return false;
    // The speaker and the actors share their IDs with the host's creatures.
    const HostLock hostLock(*host);
    int role = -1;
    if (Encounter *encounter = host->encounterOf(speaker.m_id, &role)) {
        if (role != Encounter::Npc && role != Encounter::Companion)
            return false;
        if (encounter->rewarded) {
            line(dialogs, L"Thank you for your help. Safe travels!");
        } else if (encounter->failed) {
            line(dialogs, L"It's over. We will have to try again another time.");
        } else if (encounter->kind == Encounter::Rescue) {
            line(dialogs, encounter->enemiesDefeated() ? L"The guard is down! Open the cage, quickly!"
                                                       : L"Please, get me out! The guard won't let anyone near.");
        } else if (encounter->enemiesDefeated()) {
            rewardEncounter(*encounter, &speaker);
            return true;
        } else {
            line(dialogs, encounter->kind == Encounter::Battle
                              ? L"Help us! There are too many of them!"
                              : L"The guardian is still out there, " +
                                    direction(encounter->site, speaker.m_entityData.position) +
                                    L". Bring it down and come back.");
        }
        return true;
    }
    std::array<int, 2> town {};
    if (std::uint64_t(speaker.m_id) % 3 == 1 && isTownVillager(*m_world, speaker) &&
        villageCenter(*m_world, speaker, town))
        return talkAboutVillageEncounter(speaker, town, dialogs);
    if (!canReferEncounter(*m_world, speaker))
        return false;

    // Point the player to an encounter already nearby, or find a new one.
    Encounter *chosen = nullptr;
    for (Encounter &encounter : host->m_encounters) {
        if (!encounter.accepted && !encounter.finished() &&
            distanceTo(encounter.site, player.m_entityData.position) < ReferralReuse)
            chosen = &encounter;
    }
    if (!chosen) {
        const auto kind = static_cast<Encounter::Kind>(std::uint64_t(speaker.m_id) / 2 % Encounter::RandomKindCount);
        chosen = host->startEncounter(kind, player, ReferralNearest, ReferralFarthest);
    }
    if (!chosen) {
        line(dialogs, L"The roads are quiet today. Ask me again later.");
        return true;
    }
    chosen->accepted = true;
    line(dialogs, offer(*chosen, direction(chosen->site, speaker.m_entityData.position)));
    saveEncounterProgress();
    return true;
}

bool Game::talkAboutVillageEncounter(Creature &speaker, const std::array<int, 2> &town, std::vector<Dialog> &dialogs)
{
    // talkAboutEncounter holds the host's world.
    Creature &player = *m_world->getLocalCreature();
    World &host = *encounterHost();
    if (Encounter *open = villageRequest(host, town)) {
        if (open->failed) {
            line(dialogs, L"We heard what happened out there. It's over now; there was nothing more you could do.");
            open->rewarded = true; // settled: the village may ask again
            saveEncounterProgress();
        } else if (open->siteDone()) {
            rewardEncounter(*open, &speaker, true);
        } else {
            line(dialogs, (open->kind == Encounter::Rescue ? L"They are still caged, " : L"They are still fighting, ") +
                              direction(open->site, speaker.m_entityData.position) + L". Please hurry!");
        }
        return true;
    }
    if (acceptedEncounters(host) >= ReferralCap)
        return false;

    const auto kind = std::uint64_t(speaker.m_id) / 3 % 2 ? Encounter::Rescue : Encounter::Battle;
    Encounter *chosen = nullptr;
    for (Encounter &encounter : host.m_encounters) {
        if (!encounter.accepted && !encounter.finished() && encounter.kind == kind &&
            distanceTo(encounter.site, player.m_entityData.position) < ReferralReuse)
            chosen = &encounter;
    }
    // Search outside the village rather than around the player, who usually stands inside it.
    const int radius = villageRadius(host, speaker);
    if (!chosen)
        chosen = host.startEncounter(kind, player, radius + VillageNearest, radius + VillageFarthest, &town);
    if (!chosen) {
        line(dialogs, L"It has been calm around the village lately. Thank you for asking.");
        return true;
    }
    chosen->accepted = true;
    chosen->fromTown = true;
    chosen->town = town;
    line(dialogs, villageOffer(*chosen, direction(chosen->site, speaker.m_entityData.position)));
    saveEncounterProgress();
    return true;
}

bool Game::handleEncounterObject(Creature &target, bool usePressed)
{
    // The prompt reads this game's copy; using the object changes the host's encounter and its creature.
    int role = -1;
    const Encounter *shown = m_world->encounterOf(target.m_id, &role);
    if (!shown || (role != Encounter::Object && role != Encounter::Container))
        return false;
    const plasma::Vector<3, float> offset(target.m_entityData.position -
                                          m_world->m_localCreature->m_entityData.position);
    const float reach = target.m_entityData.appearance.size[0] * 0.5f + 5.0f;
    const bool sack = role == Encounter::Object && shown->kind == Encounter::ChestSack;
    if (offset.squaredLength() > reach * reach || shown->opened || (sack && shown->keyGranted))
        return false;

    m_interface.m_inGame.m_quickButtons[QuickSlotUse].button->setText(L"info", sack ? L"Search" : L"Open", true);
    m_promptCreatureId = target.m_id;
    World *host = encounterHost();
    if (!usePressed || !host)
        return true;

    const HostLock hostLock(*host);
    Encounter *encounter = host->encounterOf(target.m_id, &role);
    if (!encounter || encounter->opened || (sack && encounter->keyGranted))
        return true;
    const auto setState = [&](int state) {
        target.m_entityData.interactionState = state;
        if (Creature *actor = host->creatureByID(target.m_id))
            actor->m_entityData.interactionState = state;
    };
    Creature &player = *m_world->getLocalCreature();
    auto *chat = m_interface.m_inGame.m_chatWidget;
    if (sack) {
        encounter->keyGranted = true;
        player.m_inventory.add(rustyKey(*encounter), 1);
        notifyItem(rustyKey(*encounter), 1, &player);
        playSound(SoundPickup);
        chat->printMessage(L"You found a Rusty Key in the sack. It opens the chest nearby.\n", EncounterColor);
    } else if (encounter->isLandmark()) {
        // The group's reward chest stands unlocked once they have all fallen.
        setState(2);
        encounter->opened = true;
        rewardEncounter(*encounter, nullptr);
    } else if (encounter->kind == Encounter::Rescue) {
        if (!encounter->enemiesDefeated()) {
            chat->printMessage(L"The cage is locked. Defeat its guardian to open it.\n", EncounterColor);
            return true;
        }
        setState(1); // the door swings open
        encounter->opened = true;
        rewardEncounter(*encounter, m_world->creatureByID(encounter->actors[Encounter::Npc]));
    } else {
        const Item key = rustyKey(*encounter);
        if (!encounter->keyGranted || player.m_inventory.countItem(key) == 0) {
            chat->printMessage(encounter->kind == Encounter::ChestSack
                                   ? L"The chest is locked. Its key lies in a sack about 56 blocks away.\n"
                                   : L"The chest is locked. Its guardian holds the key.\n",
                               EncounterColor);
            return true;
        }
        player.m_inventory.remove(key, 1);
        setState(2); // the lid opens
        encounter->opened = true;
        rewardEncounter(*encounter, nullptr);
    }
    if (encounter->accepted)
        saveEncounterProgress();
    else
        mirrorEncounters(*host);
    return true;
}

void Game::rewardEncounter(Encounter &encounter, Creature *thanking, bool inTown)
{
    if (encounter.rewarded)
        return;
    Creature &player = *m_world->getLocalCreature();
    if (encounter.fromTown && !inTown) {
        // The village that asked pays: the site only thanks the player and sends them back.
        const std::array<int, 3> town {encounter.town[0], encounter.town[1], 0};
        const std::wstring back =
            L"The village will want to thank you, " + direction(town, player.m_entityData.position) + L".";
        if (thanking) {
            std::vector<Dialog> dialogs;
            line(dialogs, L"Thank you! " + back);
            say(thanking, &player, dialogs);
        } else {
            m_interface.m_inGame.m_chatWidget->printMessage(back + L"\n", EncounterColor);
        }
        saveEncounterProgress();
        return;
    }
    encounter.rewarded = true;
    const int level = player.m_entityData.progressionLevel;
    const int gold = GoldPerStar * (encounter.level + 2) * (player.m_pyroProgression ? std::max(level, 1) : 1);
    const int playerClass = player.m_entityData.classType;
    // The native chest generators, for the player's class.
    Item reward = rand() % 2 ? m_world->randomWeapon(ItemRarityBlue, playerClass, false)
                             : m_world->randomArmor(ItemRarityBlue, playerClass, false);
    player.m_inventory.add(reward, 1);
    player.m_inventory.m_gold = std::min(player.m_inventory.m_gold, std::numeric_limits<int>::max() - gold) + gold;
    notifyItem(reward, 1, &player);
    const std::wstring text = std::to_wstring(gold) + L" gold and a piece of gear";
    if (thanking) {
        std::vector<Dialog> dialogs;
        line(dialogs,
             (inTown ? L"Word travels fast. The whole village is grateful. Take this: " : L"You did it! Take this: ") +
                 text + L".");
        say(thanking, &player, dialogs);
    } else {
        m_interface.m_inGame.m_chatWidget->printMessage(L"The chest held " + text + L".\n", EncounterColor);
    }
    saveEncounterProgress();
}

void Game::updateEncounterQuests()
{
    Creature &player = *m_world->getLocalCreature();
    World *host = encounterHost();
    // In the menus the character is not in the world yet.
    const bool live = host && player.m_playerData && m_currentCharacterSlot >= 0 && !m_world->m_worldName.empty() &&
                      !m_host.m_worldChangePending && !m_interface.m_startMenuNode->isVisible() &&
                      !m_interface.m_characterSelection.m_node->isVisible() &&
                      !m_interface.m_characterCreationNode->isVisible();
    m_world->m_encountersLive = live;
    if (!live) {
        // Without a copy, saving leaves the character's records as they were loaded (keepEncounters).
        m_world->m_encounters.clear();
        m_world->m_encountersImported = false;
        return;
    }
    // The host's threads hold its world for most of a tick: skip this frame rather than wait for them.
    if (!host->tryEnterZoneMeshes())
        return;
    if (!host->m_encountersImported && host->m_seed == m_world->m_seed && !host->m_worldName.empty())
        host->importEncounters(player.m_playerData->encounters);
    bool save = false;
    for (Encounter &encounter : host->m_encounters) {
        // A chest guardian carries the key: it falls to the player with the guardian.
        if (encounter.kind == Encounter::ChestBoss && !encounter.keyGranted && encounter.enemiesDefeated() &&
            distanceTo(encounter.site, player.m_entityData.position) < 200.0) {
            encounter.keyGranted = true;
            player.m_inventory.add(rustyKey(encounter), 1);
            notifyItem(rustyKey(encounter), 1, &player);
            m_interface.m_inGame.m_chatWidget->printMessage(
                L"The guardian dropped a Rusty Key. Its chest is close by.\n", EncounterColor);
            save |= encounter.accepted;
        }
    }
    mirrorEncounters(*host);
    host->leaveZoneMeshes();
    if (save)
        saveCharacter();
}

void Game::appendEncounterQuests(std::vector<TrackedQuest> &quests)
{
    const Creature *player = m_world->getLocalCreature();
    for (const Encounter &encounter : m_world->m_encounters) {
        if (!encounter.accepted || encounter.finished() || encounter.worldSeed != m_world->m_seed)
            continue;
        std::wstring text;
        if (encounter.fromTown && encounter.siteDone()) {
            const std::array<int, 3> town {encounter.town[0], encounter.town[1], 0};
            text = L"Return to the village for your reward (" + direction(town, player->m_entityData.position) + L")";
        } else {
            text = objective(encounter) + L" (" + direction(goal(encounter), player->m_entityData.position) + L")";
        }
        quests.push_back({TrackedQuest::Referral, encounter.worldSeed, encounter.serial, std::move(text)});
    }
}

bool Game::cancelEncounter(const TrackedQuest &quest)
{
    World *host = encounterHost();
    if (!host || !m_world->m_encountersLive)
        return false;
    const HostLock lock(*host);
    auto &encounters = host->m_encounters;
    auto found = std::find_if(encounters.begin(), encounters.end(), [&](const Encounter &encounter) {
        return encounter.serial == quest.id && encounter.worldSeed == quest.worldSeed &&
               encounter.accepted && !encounter.finished();
    });
    if (found == encounters.end())
        return false;
    // Release the referral. Its site and actors resume their ordinary overworld lifecycle.
    found->accepted = false;
    found->fromTown = false;
    mirrorEncounters(*host);
    return true;
}

void Game::keepEncounters()
{
    Creature *player = m_world->getLocalCreature();
    if (!player || !player->m_playerData)
        return;
    // Until the session has read this world's records, they stay as loaded.
    if (!m_world->m_encountersImported)
        return;
    auto &kept = player->m_playerData->encounters;
    // Keep the other worlds' records; replace this world's with the session's accepted, unfinished ones.
    kept.erase(std::remove_if(kept.begin(), kept.end(),
                              [&](const Encounter &encounter) { return encounter.worldSeed == m_world->m_seed; }),
               kept.end());
    for (const Encounter &encounter : m_world->m_encounters) {
        if (encounter.accepted && !encounter.finished())
            kept.push_back(encounter);
    }
}

} // namespace cube

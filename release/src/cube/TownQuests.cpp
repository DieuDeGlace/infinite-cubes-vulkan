// Villager requests adapted from CWVulkan Steam's CubeVulkanQuests.
#include "cube/Game.h"
#include "cube/Progression.h"
#include "cube/CreatureIds.h"
#include "cube/TravelParty.h"
#include "cube/widgets/ChatWidget.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace cube {
namespace {
// Targets come from outside the villages, beyond their radius by this margin, so the offer reaches further out.
constexpr float OfferRange = 640.0f;
constexpr float TownMargin = 32.0f;
constexpr float TargetGroupRadius = 96.0f;
constexpr int GoldPerTarget = 20;
constexpr double OctantsPerRadian = 1.2732395447351627; // 4 / pi: eight compass directions per full turn.
const plasma::Vector<4, float> QuestColor(1.0f, 0.84f, 0.40f, 1.0f);

std::array<int, 3> blockPosition(const plasma::Vector<3, Fixed> &position)
{
    return {position[0].toBlock(), position[1].toBlock(), position[2].toBlock()};
}

std::wstring creatureName(Speech &speech, int race, bool plural)
{
    const auto key = speech.creatureKey(race);
    auto name = speech.getText(key, plural ? L"plural" : L"default", L"");
    if (name.empty() && plural)
        name = speech.getText(key, L"default", L"");
    return name.empty() ? (plural ? L"enemies" : L"enemy") : name;
}

std::wstring directionTo(const std::array<int, 3> &from, const std::array<int, 3> &to)
{
    constexpr const wchar_t *Directions[] = {L"east", L"northeast", L"north", L"northwest",
                                             L"west", L"southwest", L"south", L"southeast"};
    const double x = double(to[0]) - from[0];
    const double y = double(to[1]) - from[1];
    const double distance = std::hypot(x, y);
    if (distance < 10.0)
        return L"nearby";
    const int direction = (int(std::lround(std::atan2(y, x) * OctantsPerRadian)) + 8) % 8;
    return std::to_wstring(int(distance)) + L" blocks " + Directions[direction];
}

void questLine(std::vector<Dialog> &dialogs, const std::wstring &line)
{
    writeDialogs(dialogs, line, QuestColor);
}

// Shopkeepers and the other villagers whose Talk opens a service (a shop, the identifier, crafting, flights, the guild).
bool offersService(int classType)
{
    return (classType >= ClassItemVendor && classType <= ClassAlchemist) ||
           (classType >= ClassFlightMaster && classType <= ClassMasterSailor);
}

bool canOffer(World &world, const Creature &giver)
{
    const auto &data = giver.m_entityData;
    if (data.classType != ClassVillager || data.hostilityType != HostilityFriendlyNpc || data.hp <= 0.0f ||
        (data.appearance.appearanceFlags & (AppearanceFlagPetrified | AppearanceFlagClassMaster)))
        return false;
    World::AreaInfo area;
    world.fillAreaInfo(data.position[0].toBlock(), data.position[1].toBlock(), area);
    if (area.type != PlaceVillage || area.influence <= 0.0f)
        return false;
    // A villager in a shop or beside its keeper: Talk there reaches the keeper's service, so the request could never
    // be taken or handed in.
    constexpr float ServiceReach = 10.0f;
    return std::none_of(world.m_creatures.begin(), world.m_creatures.end(), [&](const Creature *other) {
        const plasma::Vector<3, float> delta(other->m_entityData.position - data.position);
        return offersService(other->m_entityData.classType) && delta.squaredLength() < ServiceReach * ServiceReach;
    });
}

// from is where the giver stands.
bool isAvailableTarget(World &world, const plasma::Vector<3, Fixed> &from, const Creature &target,
                       const std::vector<TownQuest> &existing)
{
    const auto &data = target.m_entityData;
    if (!target.m_id || isTravelPartyMember(target.m_id) || data.hostilityType != HostilityEnemy || data.hp <= 0.0f)
        return false;
    const plasma::Vector<3, float> delta(data.position - from);
    if (delta.squaredLength() >= OfferRange * OfferRange ||
        world.nearVillage(data.position[0].toBlock(), data.position[1].toBlock(), TownMargin))
        return false;
    return std::none_of(existing.begin(), existing.end(), [&](const TownQuest &other) {
        return other.worldSeed == world.m_seed && !other.rewarded &&
               std::find(other.targets.begin(), other.targets.end(), target.m_id) != other.targets.end();
    });
}

bool selectTargets(World &world, const plasma::Vector<3, Fixed> &from, const std::vector<TownQuest> &existing,
                   TownQuest &quest)
{
    struct Candidate {
        const Creature *creature;
        float distanceSquared;
    };
    std::vector<Candidate> nearby;
    for (const Creature *creature : world.m_creatures) {
        if (!isAvailableTarget(world, from, *creature, existing))
            continue;
        const plasma::Vector<3, float> delta(creature->m_entityData.position - from);
        nearby.push_back({creature, delta.squaredLength()});
    }
    if (nearby.empty())
        return false;

    std::sort(nearby.begin(), nearby.end(), [](const Candidate &a, const Candidate &b) {
        if (a.distanceSquared != b.distanceSquared)
            return a.distanceSquared < b.distanceSquared;
        return a.creature->m_id < b.creature->m_id;
    });
    const auto &anchor = nearby.front().creature->m_entityData;
    quest.race = anchor.race;
    quest.targetPosition = blockPosition(anchor.position);
    int count = 0;
    for (const auto &candidate : nearby) {
        const auto &data = candidate.creature->m_entityData;
        const plasma::Vector<3, float> delta(data.position - anchor.position);
        if (data.race == quest.race && delta.squaredLength() <= TargetGroupRadius * TargetGroupRadius) {
            quest.targets[count++] = candidate.creature->m_id;
            if (count == TownQuest::TargetLimit)
                break;
        }
    }
    return count > 0;
}

std::wstring objective(Speech &speech, const TownQuest &quest)
{
    return L"Defeat " + std::to_wstring(quest.needed()) + L" " + creatureName(speech, quest.race, quest.needed() != 1);
}
} // namespace

void writeDialogs(std::vector<Dialog> &dialogs, const std::wstring &text, const plasma::Vector<4, float> &color)
{
    // About four lines of the speech bubble.
    constexpr size_t BubbleCharacters = 80;
    // The sentences, each with its closing mark.
    std::vector<std::wstring> pieces;
    size_t start = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const bool end =
            (text[i] == L'.' || text[i] == L'!' || text[i] == L'?') && (i + 1 == text.size() || text[i + 1] == L' ');
        if (end || i + 1 == text.size()) {
            pieces.push_back(text.substr(start, i + 1 - start));
            start = i + 2;
            i = start - 1;
        }
    }
    // A sentence too long for a bubble breaks between words.
    std::vector<std::wstring> parts;
    for (const std::wstring &piece : pieces) {
        std::wstring rest = piece;
        while (rest.size() > BubbleCharacters) {
            size_t cut = rest.rfind(L' ', BubbleCharacters);
            if (cut == std::wstring::npos || cut == 0)
                cut = BubbleCharacters;
            parts.push_back(rest.substr(0, cut));
            rest.erase(0, cut + (cut < rest.size() && rest[cut] == L' ' ? 1 : 0));
        }
        if (!rest.empty())
            parts.push_back(rest);
    }
    std::wstring bubble;
    for (const std::wstring &part : parts) {
        if (!bubble.empty() && bubble.size() + 1 + part.size() > BubbleCharacters) {
            dialogs.emplace_back();
            dialogs.back().write(bubble, color, 0);
            bubble.clear();
        }
        bubble += (bubble.empty() ? L"" : L" ") + part;
    }
    if (!bubble.empty()) {
        dialogs.emplace_back();
        dialogs.back().write(bubble, color, 0);
    }
}

std::vector<long long> nearbyTownQuestGivers(World &world, const Creature &player, float range)
{
    std::vector<long long> givers;
    if (!player.m_playerData)
        return givers;

    const auto &quests = player.m_playerData->townQuests;
    int active = 0;
    for (const auto &quest : quests) {
        if (quest.worldSeed == world.m_seed && !quest.rewarded) {
            ++active;
            if (quest.ready())
                givers.push_back(quest.giver);
        }
    }
    if (active < TownQuest::ActiveLimit) {
        for (const auto *giver : world.m_creatures) {
            if (giver->m_entityData.classType != ClassVillager || static_cast<unsigned long long>(giver->m_id) % 3)
                continue;
            const plasma::Vector<3, float> delta(giver->m_entityData.position - player.m_entityData.position);
            if (delta.squaredLength() > range * range || !canOffer(world, *giver))
                continue;
            if (std::any_of(quests.begin(), quests.end(), [&](const TownQuest &quest) {
                    return quest.worldSeed == world.m_seed && quest.giver == giver->m_id;
                }))
                continue;
            // No enemy group needs to be in sight yet: a request taken without one gets its targets later.
            givers.push_back(giver->m_id);
        }
    }
    std::sort(givers.begin(), givers.end());
    return givers;
}

bool Game::talkAboutTownQuest(Creature &giver, std::vector<Dialog> &dialogs)
{
    Creature &player = *m_world->getLocalCreature();
    if (!player.m_playerData || m_currentCharacterSlot < 0)
        return false;

    auto &quests = player.m_playerData->townQuests;
    auto found = std::find_if(quests.begin(), quests.end(), [&](const TownQuest &quest) {
        return quest.giver == giver.m_id && quest.worldSeed == m_world->m_seed;
    });
    if (found == quests.end()) {
        if (static_cast<unsigned long long>(giver.m_id) % 3 != 0 || !canOffer(*m_world, giver))
            return false;
        if (std::count_if(quests.begin(), quests.end(), [&](const TownQuest &quest) {
                return quest.worldSeed == m_world->m_seed && !quest.rewarded;
            }) >= TownQuest::ActiveLimit)
            return false;
        // Enemies only exist close to the player, so deep inside a village none may be known yet.
        TownQuest request;
        const bool sighted = selectTargets(*m_world, giver.m_entityData.position, quests, request);
        request.worldSeed = m_world->m_seed;
        request.giver = giver.m_id;
        request.giverPosition = blockPosition(giver.m_entityData.position);
        quests.push_back(request);
        questLine(dialogs, sighted ? L"Could you help our village? " + objective(m_speech, request) + L". I saw them " +
                                         directionTo(request.giverPosition, request.targetPosition) + L"."
                                   : L"Could you help our village? Something has been prowling outside it. Have a look "
                                     L"around out there and deal with what you find.");
        saveCharacter();
        return true;
    }

    TownQuest &quest = *found;
    if (quest.rewarded)
        return false;
    bool changed = false;
    if (quest.pending()) {
        changed = selectTargets(*m_world, giver.m_entityData.position, quests, quest);
        if (!changed) {
            questLine(dialogs, L"We still do not know what is out there. Please look around outside the village.");
            return true;
        }
    }
    for (const auto id : quest.targets) {
        if (id && m_world->m_state.m_deadCreatures.find(id) != m_world->m_state.m_deadCreatures.end())
            changed |= quest.recordKill(id);
    }
    if (quest.ready()) {
        const int level = player.m_entityData.progressionLevel;
        const int gold = GoldPerTarget * quest.needed() * (player.m_pyroProgression ? level : 1);
        const int playerClass = player.m_entityData.classType;
        Item reward = rand() % 2 ? m_world->randomWeapon(ItemRarityBlue, playerClass, false)
                                 : m_world->randomArmor(ItemRarityBlue, playerClass, false);
        reward.region = m_world->findRegion(quest.targetPosition[0], quest.targetPosition[1]);
        if (player.m_pyroProgression) {
            reward.setGearLevel(
                progression::lootLevel(level, level, reward.category, reward.id, reward.modifier, reward.material));
            player.awardExperience(progression::killXp(3, level, level) * quest.needed());
        }
        player.m_inventory.add(reward, 1, -1);
        player.m_inventory.m_gold = std::min(player.m_inventory.m_gold, std::numeric_limits<int>::max() - gold) + gold;
        quest.rewarded = true;
        notifyItem(reward, 1, &player);
        questLine(dialogs, L"Good work! Thank you. Here are " + std::to_wstring(gold) + L" gold and a piece of gear.");
        changed = true;
    } else {
        questLine(dialogs, objective(m_speech, quest) + L". The group was " +
                               directionTo(blockPosition(giver.m_entityData.position), quest.targetPosition) + L".");
    }
    if (changed)
        saveCharacter();
    return true;
}

bool Game::updateTownQuest(const ServerUpdates::KillPacket &kill)
{
    Creature *player = m_world->getLocalCreature();
    if (!player->m_playerData || m_currentCharacterSlot < 0 || !kill.killerId)
        return false;
    const Creature *killer = m_world->creatureByID(kill.killerId);
    if (kill.killerId != player->m_id && (!killer || killer->m_entityData.hostilityType != HostilityPlayer))
        return false;

    bool changed = false;
    for (auto &quest : player->m_playerData->townQuests) {
        if (quest.worldSeed != m_world->m_seed || !quest.recordKill(kill.targetId))
            continue;
        changed = true;
        if (quest.ready())
            m_interface.m_inGame.m_chatWidget->printMessage(L"Request complete. Return to the villager.\n", QuestColor);
    }
    return changed;
}

bool Game::assignTownQuestTargets()
{
    Creature *player = m_world->getLocalCreature();
    if (!player->m_playerData || m_currentCharacterSlot < 0)
        return false;
    auto &quests = player->m_playerData->townQuests;
    const bool waiting = std::any_of(quests.begin(), quests.end(), [&](const TownQuest &quest) {
        return quest.worldSeed == m_world->m_seed && !quest.rewarded && quest.pending();
    });
    // The search goes through every creature, so twice a second.
    using Clock = std::chrono::steady_clock;
    static Clock::time_point lastSearch;
    const auto now = Clock::now();
    if (!waiting || now - lastSearch < std::chrono::milliseconds(500))
        return false;
    lastSearch = now;

    bool changed = false;
    for (auto &quest : quests) {
        if (quest.worldSeed != m_world->m_seed || quest.rewarded || !quest.pending())
            continue;
        const plasma::Vector<3, Fixed> from(Fixed(quest.giverPosition[0]), Fixed(quest.giverPosition[1]),
                                            Fixed(quest.giverPosition[2]));
        if (!selectTargets(*m_world, from, quests, quest))
            continue;
        changed = true;
        m_interface.m_inGame.m_chatWidget->printMessage(
            L"The villager's request: " + objective(m_speech, quest) + L", " +
                directionTo(blockPosition(player->m_entityData.position), quest.targetPosition) + L".\n",
            QuestColor);
    }
    return changed;
}

std::wstring Game::townQuestSummary()
{
    const Creature *player = m_world->getLocalCreature();
    if (!player || !player->m_playerData || m_currentCharacterSlot < 0)
        return {};
    const auto current = blockPosition(player->m_entityData.position);
    std::wstring text;
    for (const auto &quest : player->m_playerData->townQuests) {
        if (quest.worldSeed != m_world->m_seed || quest.rewarded)
            continue;
        if (!text.empty())
            text += L"\n";
        if (quest.ready()) {
            text += L"Return to the villager (" + directionTo(current, quest.giverPosition) + L")";
        } else if (quest.pending()) {
            text += L"Look for what troubles the village, outside it";
        } else {
            text += objective(m_speech, quest) + L" (" + std::to_wstring(quest.progress()) + L"/" +
                    std::to_wstring(quest.needed()) + L", " + directionTo(current, quest.targetPosition) + L")";
        }
    }
    return text;
}

} // namespace cube

#include "cube/TownQuest.h"
#include "cube/BytesIO.h"
#include "cube/GameController.h"
#include "cube/Creature.h"
#include "cube/CreatureIds.h"
#include "cube/Speech.h"
#include <cmath>
#include <tuple>

namespace cube {
namespace {
constexpr float OfferRange = 640.f;
constexpr float GroupRadius = 96.f;
constexpr int GoldPerTarget = 20;
constexpr uint32_t SaveTag = 0x31515441; // ATQ1, after the stock character payload.
constexpr size_t RecordBytes = 2 * sizeof(int32_t) + 6 * sizeof(int32_t) + 6 * sizeof(int64_t) + 2;
const plasma::Vector<4, float> QuestColor(1.f, 0.84f, 0.4f, 1.f);

// Network creature IDs are temporary. A generated Alpha spawn keeps the same zone and slot when reloaded.
long long spawnKey(const Creature &c)
{
    const auto &s = c.m_entityData.spawnSlot;
    if (s[0] < 0 || s[0] > 65535 || s[1] < 0 || s[1] > 65535 || s[2] < 0)
        return 0;
    return static_cast<long long>((uint64_t(s[0]) << 48) | (uint64_t(s[1]) << 32) | (uint32_t(s[2]) + 1u));
}

std::array<int, 3> position(const Creature &c)
{
    const auto block = toBlock(c.m_entityData.position);
    return {block[0], block[1], block[2]};
}

bool isSettlement(World &world, const Creature &c)
{
    const auto &s = c.m_entityData.spawnSlot;
    auto *cell = world.getCell(s[0], s[1]);
    return cell && cell->kind == Region::Cell::StructureQuarter;
}

std::wstring direction(const std::array<int, 3> &from, const std::array<int, 3> &to)
{
    const double x = double(to[0]) - from[0], y = double(to[1]) - from[1];
    constexpr const wchar_t *points[] = {L"east", L"northeast", L"north", L"northwest",
                                         L"west", L"southwest", L"south", L"southeast"};
    return points[(int(std::lround(std::atan2(y, x) * (4.0 / 3.141592653589793))) + 8) % 8];
}

bool selectTargets(World &world, const std::vector<TownQuest> &quests, TownQuest &q)
{
    struct Candidate {
        const Creature *creature;
        long long key;
        float distance;
    };
    std::vector<Candidate> candidates;
    const plasma::Vector<3, Fixed> giver(Fixed(q.giverPosition[0]), Fixed(q.giverPosition[1]),
                                         Fixed(q.giverPosition[2]));
    for (const auto &entry : world.m_creatures) {
        const Creature &c = *entry.second;
        if (c.m_entityData.hostilityType != HostilityEnemy || c.m_entityData.hp <= 0)
            continue;
        const auto key = spawnKey(c);
        if (!key || isSettlement(world, c))
            continue;
        const float distance = plasma::Vector<3, float>(c.m_entityData.position - giver).squaredLength();
        if (distance > OfferRange * OfferRange)
            continue;
        const bool claimed = std::any_of(quests.begin(), quests.end(), [&](const TownQuest &other) {
            return other.worldSeed == q.worldSeed && !other.rewarded &&
                   std::find(other.targets.begin(), other.targets.end(), key) != other.targets.end();
        });
        if (!claimed)
            candidates.push_back({&c, key, distance});
    }
    if (candidates.empty())
        return false;
    std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
        return std::tie(a.distance, a.key) < std::tie(b.distance, b.key);
    });
    const Creature &anchor = *candidates.front().creature;
    q.race = anchor.m_entityData.race;
    q.targetPosition = position(anchor);
    size_t count = 0;
    for (const auto &candidate : candidates) {
        const Creature &c = *candidate.creature;
        if (c.m_entityData.race != q.race ||
            plasma::Vector<3, float>(c.m_entityData.position - anchor.m_entityData.position).squaredLength() >
                GroupRadius * GroupRadius)
            continue;
        q.targets[count++] = candidate.key;
        if (count == q.targets.size())
            break;
    }
    return true;
}
std::wstring objective(World &world, const TownQuest &q)
{
    if (q.pending())
        return L"Look for enemies outside the village.";
    if (q.ready())
        return L"Return to the villager for your reward.";
    auto label = world.m_speech.getForm(world.creatureKey(q.race), q.needed() == 1 ? L"singular" : L"plural");
    return L"Defeat " + label + L" (" + std::to_wstring(q.progress()) + L"/" + std::to_wstring(q.needed()) + L")";
}
}

bool isTownQuestGiver(World &world, const Creature &giver)
{
    const auto &d = giver.m_entityData;
    if (!spawnKey(giver) || d.hp <= 0 || d.hostilityType != HostilityFriendlyNpc || d.classType >= ClassItemVendor ||
        (d.appearance.appearanceFlags & (1 << AppearanceFlagClassMaster)) || !isSettlement(world, giver))
        return false;
    for (const auto &entry : world.m_creatures) {
        const auto &other = entry.second->m_entityData;
        if (other.classType >= ClassItemVendor &&
            plasma::Vector<3, float>(other.position - d.position).squaredLength() < 100.f)
            return false;
    }
    return true;
}

bool GameController::hasTownQuest(const Creature &giver)
{
    if (!m_player || !m_player->m_playerData)
        return false;
    const auto key = spawnKey(giver);
    const auto &quests = m_player->m_playerData->townQuests;
    return std::any_of(quests.begin(), quests.end(), [&](const TownQuest &q) {
        return q.giver == key && q.worldSeed == m_world.getSeed() && !q.rewarded;
    });
}

bool GameController::talkAboutTownQuest(Creature &giver, std::vector<Dialog> &dialogs)
{
    if (!m_player || !m_player->m_playerData || m_selectedCharacter < 0 ||
        (!hasTownQuest(giver) && !isTownQuestGiver(m_world, giver)))
        return false;
    auto &list = m_player->m_playerData->townQuests;
    const int seed = m_world.getSeed();
    auto q = std::find_if(list.begin(), list.end(),
                          [&](const TownQuest &v) { return v.worldSeed == seed && v.giver == spawnKey(giver); });
    auto line = [&](const std::wstring &s) {
        dialogs.emplace_back();
        dialogs.back().write(s, QuestColor);
    };
    bool changed = false;
    if (q == list.end()) {
        if (std::count_if(list.begin(), list.end(), [&](const TownQuest &v) {
                return v.worldSeed == seed && !v.rewarded;
            }) >= TownQuest::ActiveLimit)
            return false;
        TownQuest request;
        request.worldSeed = seed;
        request.giver = spawnKey(giver);
        request.giverPosition = position(giver);
        selectTargets(m_world, list, request);
        list.push_back(request);
        q = std::prev(list.end());
        line(L"Could you help our village?");
        changed = true;
    } else if (q->rewarded) {
        return false;
    } else if (q->ready()) {
        const int gold = GoldPerTarget * q->needed();
        Item coins;
        coins.category = ItemCategoryCoin;
        coins.material = ItemMaterialGold;
        coins.level = short(gold);
        m_player->m_inventory.add(coins);
        Item reward;
        reward.category = ItemCategoryRing;
        reward.material = ItemMaterialSilver;
        reward.rarity = 2;
        reward.level = short(std::clamp(m_player->m_entityData.level, 1, 32767));
        reward.modifier = uint32_t(q->giver) ^ uint32_t(seed);
        m_player->m_inventory.add(reward);
        m_player->m_entityData.xp += q->needed() * std::max(1, m_player->m_entityData.level);
        m_player->applyLevelUps(&m_world);
        q->rewarded = true;
        notifyItem(reward, m_player);
        line(L"Good work! Here are " + std::to_wstring(gold) + L" gold and a piece of gear.");
        changed = true;
    }
    if (!q->rewarded) {
        line(objective(m_world, *q));
        if (!q->pending())
            line(L"The group was " + direction(q->giverPosition, q->targetPosition) + L" of here.");
    }
    if (changed) {
        m_townQuestSearchDelay = 0;
        saveCharacter(m_selectedCharacter, m_player);
    }
    return true;
}

bool GameController::recordTownQuestKill(const KillPacket &kill)
{
    if (!m_player || !m_player->m_playerData || !kill.killerId ||
        (kill.killerId != m_player->m_id && kill.killerId != m_player->m_petId &&
         kill.killerId != m_player->m_companionId))
        return false;
    const auto *target = m_world.creatureByID(kill.targetId);
    if (!target)
        return false;
    bool changed = false;
    for (auto &q : m_player->m_playerData->townQuests)
        if (q.worldSeed == m_world.getSeed())
            changed |= q.recordKill(spawnKey(*target));
    return changed;
}

bool GameController::updateTownQuests(int elapsed)
{
    if (!m_player || !m_player->m_playerData || m_selectedCharacter < 0)
        return false;
    m_townQuestSearchDelay -= elapsed;
    if (m_townQuestSearchDelay > 0)
        return false;
    m_townQuestSearchDelay = 500;
    auto &list = m_player->m_playerData->townQuests;
    bool changed = false;
    for (auto &q : list)
        if (!q.rewarded && q.pending() && q.worldSeed == m_world.getSeed())
            changed |= selectTargets(m_world, list, q);
    auto &markers = m_worldMap.m_townQuestMarkers;
    markers.clear();
    const int seed = m_world.getSeed();
    for (const auto &quest : list) {
        if (quest.worldSeed != seed || quest.rewarded || quest.pending())
            continue;
        const auto &pos = quest.ready() ? quest.giverPosition : quest.targetPosition;
        markers.push_back({{float(pos[0]), float(pos[1]), float(pos[2]) + 3.f}, gfx::QuestIcon::Location});
    }
    if (std::count_if(list.begin(), list.end(), [&](const TownQuest &q) {
            return q.worldSeed == seed && !q.rewarded;
        }) < TownQuest::ActiveLimit) {
        for (const auto &entry : m_world.m_creatures) {
            const auto &npc = *entry.second;
            if (npc.m_entityData.hostilityType != HostilityFriendlyNpc || !isTownQuestGiver(m_world, npc))
                continue;
            const auto key = spawnKey(npc);
            if (std::any_of(list.begin(), list.end(),
                            [&](const TownQuest &q) { return q.worldSeed == seed && q.giver == key; }))
                continue;
            auto pos = plasma::Vector<3, float>(npc.m_entityData.position);
            pos[2] += 3.f;
            markers.push_back({pos, gfx::QuestIcon::Giver});
        }
    }
    return changed;
}

std::wstring GameController::townQuestSummary()
{
    if (!m_player || !m_player->m_playerData)
        return {};
    std::wstring summary;
    for (const auto &q : m_player->m_playerData->townQuests) {
        if (q.worldSeed != m_world.getSeed() || q.rewarded)
            continue;
        if (!summary.empty())
            summary += L"\n";
        summary += objective(m_world, q);
    }
    return summary;
}

void saveTownQuests(BytesIO &bytes, const std::vector<TownQuest> &quests)
{
    if (quests.empty())
        return;
    const auto count = uint32_t(quests.size());
    bytes.write(&SaveTag, 4);
    bytes.write(&count, 4);
    for (auto &q : quests) {
        bytes.write(&q.worldSeed, 4);
        bytes.write(&q.giver, 8);
        bytes.write(&q.race, 4);
        bytes.write(q.giverPosition.data(), 12);
        bytes.write(q.targetPosition.data(), 12);
        bytes.write(q.targets.data(), 40);
        bytes.write(&q.defeated, 1);
        const uint8_t rewarded = q.rewarded;
        bytes.write(&rewarded, 1);
    }
}
void loadTownQuests(BytesIO &bytes, std::vector<TownQuest> &quests)
{
    quests.clear();
    if (bytes.size() - bytes.m_offset < 8)
        return;
    uint32_t tag, count;
    bytes.read(&tag, 4);
    bytes.read(&count, 4);
    if (tag != SaveTag || count > size_t(bytes.size() - bytes.m_offset) / RecordBytes)
        return;
    quests.resize(count);
    for (auto &q : quests) {
        bytes.read(&q.worldSeed, 4);
        bytes.read(&q.giver, 8);
        bytes.read(&q.race, 4);
        bytes.read(q.giverPosition.data(), 12);
        bytes.read(q.targetPosition.data(), 12);
        bytes.read(q.targets.data(), 40);
        bytes.read(&q.defeated, 1);
        uint8_t rewarded;
        bytes.read(&rewarded, 1);
        q.rewarded = rewarded != 0;
    }
}
}

#include "cube/World.h"
#include "cube/Progression.h"

namespace cube {

int World::regionLevel(const plasma::Vector<2, int> &region) const
{
    if (m_localCreature)
        return m_localCreature->m_entityData.progressionLevel;
    if (m_soloLevel)
        return m_soloLevel;
    const auto found = m_regionLevels.find({region[0], region[1]});
    return found == m_regionLevels.end() ? 1 : found->second;
}

void World::updateProgressionLevels()
{
    // The first player in a region sets the shared difficulty while that region is occupied.
    std::set<std::array<int, 2>> occupied;
    int players = 0;
    int currentLevel = 1;
    bool existingRegion = false;
    for (const auto *creature : m_creatures) {
        const auto &data = creature->m_entityData;
        if (data.hostilityType != HostilityPlayer)
            continue;
        const auto region = findRegion(data.position[0].toBlock(), data.position[1].toBlock());
        const std::array<int, 2> key = {region[0], region[1]};
        currentLevel = std::clamp(data.progressionLevel, 1, progression::MaxLevel);
        ++players;
        occupied.insert(key);
        existingRegion |= !m_regionLevels.try_emplace(key, currentLevel).second;
    }
    const bool shared = players > 1 || (players == 1 && m_soloLevel == 0 && existingRegion);
    m_soloLevel = shared ? 0 : currentLevel;
    if (players == 1 && !shared)
        m_regionLevels[*occupied.begin()] = currentLevel;
    for (auto it = m_regionLevels.begin(); it != m_regionLevels.end();) {
        if (!occupied.count(it->first))
            it = m_regionLevels.erase(it);
        else
            ++it;
    }
    for (auto *creature : m_creatures) {
        auto &data = creature->m_entityData;
        if (data.hp <= 0.0f || data.hostilityType != HostilityEnemy)
            continue;
        const auto &region = data.region;
        const int next = progression::enemyLevel(regionLevel(region), landBiome(region), m_seed, region[0], region[1],
                                                 creature->m_id);
        if (next != data.progressionLevel) {
            const float health = data.hp / creature->maxHP();
            data.progressionLevel = next;
            data.hp = health * creature->maxHP();
        }
    }
}

std::pair<long long, int> World::progressionKillCredit(Creature *attacker, const Creature &target)
{
    const auto &victim = target.m_entityData;
    if (!m_pyroProgression || m_isClient || !attacker ||
        (victim.hostilityType != HostilityEnemy && victim.hostilityType != HostilityNeutral) ||
        victim.appearance.hasFlag(AppearanceFlagSummoned))
        return {};
    if (attacker->m_entityData.hostilityType == HostilityPet)
        attacker = creatureByID(attacker->m_entityData.ownerId);
    if (!attacker || attacker->m_entityData.hostilityType != HostilityPlayer)
        return {};

    const int experience = progression::killXp(
        std::clamp(victim.level, 0, 4), victim.progressionLevel,
        std::clamp(attacker->m_entityData.progressionLevel, 1, progression::MaxLevel),
        victim.appearance.hasFlag(AppearanceFlagLarge), victim.appearance.hasFlag(AppearanceFlagMissionResident),
        victim.appearance.hasFlag(AppearanceFlagEnhanced));
    return {attacker->m_id, experience};
}

} // namespace cube

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "plasma/Vector.h"

namespace cube {

class Creature;
class World;
struct Dialog;

// Appends text as speech dialogs that each fit one bubble, split between sentences (or words, for a long sentence);
// the speech widget shows them one after another.
void writeDialogs(std::vector<Dialog> &dialogs, const std::wstring &text, const plasma::Vector<4, float> &color);

struct TownQuest {
    static constexpr int ActiveLimit = 3;
    static constexpr int TargetLimit = 5;

    int worldSeed = 0;
    long long giver = 0;
    int race = 0;
    std::array<int, 3> giverPosition {};
    std::array<int, 3> targetPosition {};
    std::array<long long, TargetLimit> targets {};
    std::uint8_t defeated = 0;
    bool rewarded = false;

    int needed() const { return int(std::find(targets.begin(), targets.end(), 0) - targets.begin()); }

    int progress() const
    {
        int count = 0;
        for (int i = 0; i < needed(); ++i)
            count += (defeated >> i) & 1;
        return count;
    }

    bool ready() const { return needed() > 0 && progress() == needed(); }
    // Taken while no enemy group was in sight: the targets are chosen once the player comes upon one.
    bool pending() const { return needed() == 0; }

    bool recordKill(long long id)
    {
        if (rewarded || !id)
            return false;
        const auto found = std::find(targets.begin(), targets.end(), id);
        if (found == targets.end())
            return false;
        const auto bit = std::uint8_t(1u << (found - targets.begin()));
        if (defeated & bit)
            return false;
        defeated |= bit;
        return true;
    }
};

// The givers waiting for the player's return, and the villagers within range that have a request to offer. Sorted.
std::vector<long long> nearbyTownQuestGivers(World &world, const Creature &player, float range);

}

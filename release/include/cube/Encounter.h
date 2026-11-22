// Overworld encounters adapted from DieuDeGlace's OverworldEvents mod for Steam Cube World (MIT): a battle beside two
// adventurers, a rescue from a guarded cage, a bounty on a named guardian, a chest whose key lies in a sack or with a
// guardian, and the landmark groups that hold natural places (a hill's boss and pack, a camp's leader and followers, a
// shrine's guardian and minions) with a reward chest once they fall. World::updateEncounters plans and stages them;
// Game (Encounters.cpp) runs their talk, objects and rewards.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace cube {

struct Encounter {
    enum Kind : std::uint8_t {
        Battle,
        Rescue,
        Bounty,
        ChestSack,
        ChestBoss,
        Hill,
        Camp,
        Shrine,
        KindCount
    };
    // The kinds that turn up anywhere; the landmark kinds belong to native places.
    static constexpr int RandomKindCount = Hill;
    // Actor slots: up to six enemies (the guardian is enemy 0), the adventurer or captive, a second adventurer, the cage
    // or sack, and the chest.
    enum Role {
        FirstEnemy = 0,
        Npc = 6,
        Companion = 7,
        Object = 8,
        Container = 9,
        RoleCount = 10
    };
    static constexpr int MaxEnemies = 6;

    std::uint32_t serial = 0;
    int worldSeed = 0;
    Kind kind = Battle;
    std::array<int, 3> site {};     // blocks: the centre, the cage or the chest
    std::array<int, 3> sackSite {}; // blocks: ChestSack's sack
    std::uint32_t roll = 0;
    std::array<int, MaxEnemies> races {};
    std::uint8_t enemyCount = 0;
    std::uint8_t level = 0; // native stars of the enemies; the guardian has one more
    int progressionLevel = 1;
    std::uint8_t defeated = 0; // a bit per enemy
    bool keyGranted = false;   // the chest's Rusty Key was found
    bool opened = false;       // the cage or chest was opened
    bool rewarded = false;
    bool failed = false;   // Battle: both adventurers fell
    bool accepted = false; // a traveler pointed the player here: shown as an objective and saved with the character
    std::array<int, 2> place {}; // a landmark's native place cell
    bool fromTown = false;       // a villager asked: the reward waits in the village (town) once the site is done
    std::array<int, 2> town {};  // that village's centre, in blocks

    // Not saved: the actors staged for the current visit, and whether the sites stand on the loaded ground (a site
    // chosen where the world was not loaded yet comes from the terrain's height and is settled when it is staged).
    std::array<long long, RoleCount> actors {};
    bool grounded = false;

    bool isLandmark() const { return kind == Hill || kind == Camp || kind == Shrine; }
    // A named boss leads as enemy 0: a guardian, or a landmark group's leader.
    bool hasGuardian() const { return kind == Rescue || kind == Bounty || kind == ChestBoss || isLandmark(); }
    bool hasChest() const { return kind == ChestSack || kind == ChestBoss; } // locked, with a Rusty Key
    bool hasAdventurer() const { return kind == Battle || kind == Bounty; }
    std::uint8_t allEnemies() const { return std::uint8_t((1u << enemyCount) - 1u); }
    bool enemiesDefeated() const { return (defeated & allEnemies()) == allEnemies(); }
    bool finished() const { return rewarded || failed; }
    // The work at the site is done: the enemies are down, or the cage or chest is open.
    bool siteDone() const { return kind == Battle || kind == Bounty ? enemiesDefeated() : opened; }

    // The named boss's name (the mod's syllables), drawn from the roll.
    std::string bossName() const
    {
        static constexpr const char *First[] = {"Vor", "Mor", "Kel", "Zar", "Ash", "Grim", "Bel", "Fen"};
        static constexpr const char *Last[] = {"gath", "drak", "thorn", "mire", "gorn", "zul", "bane", "fang"};
        return std::string(First[roll % 8]) + Last[(roll >> 16) % 8];
    }

    // The mod's small, bounded premium on an encounter enemy's maximum HP and attack, by the difficulty level: the
    // player's level or the region's, whichever is higher.
    static float healthPremium(int level)
    {
        return 1.0f + std::min(0.40f, 0.02f * std::sqrt(float(std::max(level, 1) - 1)));
    }
    static float damagePremium(int level)
    {
        return 1.0f + std::min(0.25f, 0.0125f * std::sqrt(float(std::max(level, 1) - 1)));
    }

    // Battle: four to six; ChestSack: none; a hill's boss with three to five, a camp's leader with four or five, a
    // shrine's guardian with two or three; the others: the guardian alone.
    static std::uint8_t enemiesFor(Kind kind, std::uint32_t roll)
    {
        switch (kind) {
        case Battle:
            return std::uint8_t(4 + (roll >> 16) % 3);
        case ChestSack:
            return 0;
        case Hill:
            return std::uint8_t(4 + (roll >> 16) % 3);
        case Camp:
            return std::uint8_t(5 + (roll >> 16) % 2);
        case Shrine:
            return std::uint8_t(3 + (roll >> 16) % 2);
        default:
            return 1;
        }
    }

    // The native place types that hold a landmark group (the mod's table): settlements, dungeons, lakes, lava lakes
    // and mana pumps hold none.
    static bool landmarkKind(int placeType, Kind &kind)
    {
        switch (placeType) {
        case 10:
        case 18:
        case 19:
        case 20:
        case 21:
        case 23:
        case 24:
        case 25: // Mountain .. Forest
        case 26:
        case 27:
        case 28:
        case 29:
        case 30:
        case 31:
        case 32:
        case 33: // DarkForest .. Glade
        case 36: // Beach
            kind = Hill;
            return true;
        case 22:
        case 34:
        case 35: // OldHut, Camp, BanditCamp
            kind = Camp;
            return true;
        case 9:
        case 16:
        case 37:
        case 38:
        case 39: // Graveyard, CircleOfPower, Altar, Sanctuary, Menhir
            kind = Shrine;
            return true;
        default:
            return false;
        }
    }
};

} // namespace cube

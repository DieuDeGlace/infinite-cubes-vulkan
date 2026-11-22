#pragma once
#include "plasma/Vector.h"

namespace gfx {

// The sprites of the quest-icon atlas (from OverworldEvents): an orange "!" over a giver, an orange "?" for an
// objective to find or a hand-in, and a grey "?" for an accepted objective.
enum class QuestIcon {
    Giver,
    Location,
    Objective
};

struct QuestMarker {
    plasma::Vector<3, float> position;
    QuestIcon icon;
};

}

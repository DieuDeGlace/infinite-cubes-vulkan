#pragma once
#include "plasma/Vector.h"

namespace gfx {

enum class QuestIcon {
    Giver,
    Location
};

struct QuestMarker {
    plasma::Vector<3, float> position;
    QuestIcon icon;
};

}

#pragma once
#include "plasma/Vector.h"

namespace cube {

struct HairGradient {
    bool enabled = false;
    plasma::Vector<3, unsigned char> tips {255, 255, 255};
    unsigned char start = 40;
    unsigned char blend = 35;
};

}

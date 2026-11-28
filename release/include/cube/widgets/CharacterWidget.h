#pragma once
#include "cube/widgets/BaseWidget.h"

namespace plasma {
class Node;
class Texture;
}

namespace cube {

class Game;

class CharacterWidget : public BaseWidget {
public:
    CharacterWidget(Game *game, plasma::Node *node);

    virtual void draw();
    // CharacterWidget displays the local character's statistics and equipment details.

    Game *m_game;
    // Alpha's skill icons beside the climbing and swimming rows.
    plasma::Texture *m_climbingIcon = nullptr;
    plasma::Texture *m_swimmingIcon = nullptr;
    plasma::Texture *m_ridingIcon = nullptr;
    plasma::Texture *m_tamingIcon = nullptr;
};

}

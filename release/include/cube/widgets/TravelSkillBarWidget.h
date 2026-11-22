#pragma once
#include "cube/widgets/BaseWidget.h"

namespace plasma {
class Node;
class Texture;
}

namespace cube {

class Game;

// Below the coin: a bar for each of swimming, climbing and riding trained in the last minute, with its icon, level and
// progress to the next level, fading out after about a minute (Game::travelBarAlpha).
class TravelSkillBarWidget : public BaseWidget {
public:
    TravelSkillBarWidget(Game *game, plasma::Node *node);

    virtual void draw();
    // The rows drawn now: the skills with a visible bar.
    int visibleRows() const;

    static constexpr float RowHeight = 32.0f; // a 26-unit icon and a gap

    Game *m_game;
    plasma::Texture *m_icons[3] = {}; // swimming, climbing, riding
};

}

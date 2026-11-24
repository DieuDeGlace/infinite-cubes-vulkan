#pragma once
#include "cube/widgets/BaseWidget.h"

namespace plasma {
class Node;
class Texture;
}

namespace cube {

class Game;

// At the middle left: a row for each of swimming, climbing and riding trained in the last minute, fading out after
// about a minute (Game::travelBarAlpha). The widget draws each row's icon; Interface places a copy of the level bar
// ("levelinfo") beside it with the name, level and progress to the next level.
class TravelSkillBarWidget : public BaseWidget {
public:
    TravelSkillBarWidget(Game *game, plasma::Node *node);

    // One skill's row: alpha 0 when it is not shown.
    struct Row {
        float alpha = 0.0f;
        int level = 0;
        float progress = 0.0f; // towards the next level, 1 at the cap
    };

    virtual void draw();
    // The rows drawn now: the skills with a visible bar.
    int visibleRows() const;
    Row row(int skill) const;

    static constexpr const wchar_t *Names[] = {L"Swimming", L"Climbing", L"Riding"};
    static constexpr float RowHeight = 32.0f; // a 26-unit icon and a gap
    static constexpr float IconSize = 26.0f;
    static constexpr float IconGap = 6.0f; // between the icon and its bar

    Game *m_game;
    plasma::Texture *m_icons[3] = {}; // swimming, climbing, riding
};

}

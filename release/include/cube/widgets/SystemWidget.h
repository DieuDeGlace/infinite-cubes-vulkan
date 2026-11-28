#pragma once
#include "cube/widgets/BaseWidget.h"
#include "cube/TownQuest.h"

namespace plasma {
class Node;
}

namespace cube {

class Game;

class SystemWidget : public BaseWidget {
public:
    SystemWidget(Game *game, plasma::Node *node);

    virtual void draw();
    void buttonReleased();
    void showQuestList(bool open);

protected:
    Game *m_game;
    int m_hoveredButton;
    bool m_choosingQuest = false;
    int m_questCharacterSlot = -1;
    std::vector<TrackedQuest> m_quests;
};

}

// See cube/SystemWidget.h.
#include "plasma/TextIds.h"
#include "cube/Game.h"
#include "cube/widgets/SystemWidget.h"
#include "cube/widgets/ChatWidget.h"
#include "cube/World.h"
#include "plasma/Engine.h"
#include "plasma/Display.h"
#include "plasma/Node.h"

#include "cube/InterfaceIds.h"
#include "plasma/EditFilter.h"

namespace cube {

SystemWidget::SystemWidget(Game *game, plasma::Node *node)
    : BaseWidget(game->m_engine, node, L""),
      m_game(game),
      m_hoveredButton(-1)
{
    setFont(L"resource1.dat");
    connect(plasma::WIDGET_EVENT_POINTER_RELEASED, this, &SystemWidget::buttonReleased, true, true);
}

void SystemWidget::showQuestList(bool open)
{
    m_choosingQuest = open;
    m_hoveredButton = -1;
    m_quests.clear();
    if (open) {
        m_questCharacterSlot = m_game->m_currentCharacterSlot;
        m_quests = m_game->m_interface.m_inGame.m_trackedQuests;
    }
    const int rows = open ? 2 + (int)m_quests.size() : 5;
    resizeScaled(open ? 400.0f : 150.0f, 30.0f * (rows + 1), true);
    const plasma::Vector<2, float> window((float)m_game->m_width, (float)m_game->m_height);
    m_node->setPosition((window - screenSize()) * 0.5f, plasma::Vector<2, float>(0.0f, 0.0f));
}

void SystemWidget::draw()
{
    const float center = getContentWidth() * 0.5f;
    const auto pointer = getPointerOffset();
    m_hoveredButton = -1;
    setTextStrokeRadius(3.0f);
    setTextStrokeColor(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f));
    setTextFlags(1 << plasma::TEXT_CENTER);
    const auto row = [&](int index, const std::wstring &label, bool button = true) {
        const int y = 30 * (index + 1);
        const bool hovered = button && pointer[0] >= 0 && pointer[0] < getContentWidth() &&
                             pointer[1] >= y - 20 && pointer[1] < y + 10;
        if (hovered)
            m_hoveredButton = index;
        setTextColor(plasma::Vector<4, float>(hovered ? 0.2f : 1.0f, 1.0f, 1.0f, 1.0f));
        setTextSize(13.0f);
        if (m_choosingQuest && button) {
            plasma::Vector<2, float> low, high;
            measureText(label, low, high);
            const float width = high[0] - low[0];
            if (width > getContentWidth() - 20)
                setTextSize(13.0f * (getContentWidth() - 20) / width);
        }
        renderText(label, center, (float)y);
        if (button)
            m_game->m_interface.addButtonCenter(this, (int)center, y - 5);
    };
    if (m_choosingQuest) {
        row(0, m_quests.empty() ? L"No active quests" : L"Choose a quest to cancel", false);
        for (size_t i = 0; i < m_quests.size(); ++i)
            row((int)i + 1, m_quests[i].description);
        row((int)m_quests.size() + 1, L"Back");
    } else {
        row(SystemMenuOptions, L"Options");
        row(SystemMenuControls, L"Controls");
        row(SystemMenuStartMenu, L"Start Menu");
        row(SystemMenuExit, L"Exit Game");
        row(SystemMenuCancelQuest, L"Cancel a quest");
    }
}

void SystemWidget::buttonReleased()
{
    if (m_hoveredButton < 0)
        return;
    if (m_choosingQuest) {
        const int index = m_hoveredButton - 1;
        if (index >= 0 && index < (int)m_quests.size() && m_game->m_currentCharacterSlot == m_questCharacterSlot) {
            const bool canceled = m_game->cancelQuest(m_quests[index]);
            m_game->m_interface.m_inGame.m_chatWidget->printMessage(
                canceled ? L"Quest canceled.\n" : L"That quest is no longer active.\n",
                plasma::Vector<4, float>(1.0f, 0.84f, 0.40f, 1.0f));
        }
        showQuestList(false);
        return;
    }
    if (m_hoveredButton == SystemMenuCancelQuest) {
        showQuestList(true);
        return;
    }
    m_node->setVisible(false);
    switch (m_hoveredButton) {
    case SystemMenuOptions:
        m_game->m_interface.toggleOptionsWidget();
        break;
    case SystemMenuControls:
        m_game->m_interface.toggleControlsWidget();
        break;
    case SystemMenuStartMenu:
        m_game->onStartMenu();
        break;
    case SystemMenuExit:
        m_game->onExit();
        break;
    }
}

}

// See cube/widgets/TravelSkillBarWidget.h.
#include <sstream>
#include "cube/widgets/TravelSkillBarWidget.h"
#include "cube/Creature.h"
#include "cube/Game.h"
#include "cube/SkillIcons.h"
#include "cube/World.h"
#include "plasma/Engine.h"
#include "plasma/Texture.h"

namespace cube {

TravelSkillBarWidget::TravelSkillBarWidget(Game *game, plasma::Node *node)
    : BaseWidget(game->m_engine, node, L""),
      m_game(game)
{
    setFont(L"resource1.dat");
    plasma::TextureFormat format;
    format.minFilter = format.maxFilter = plasma::TEXTURE_FILTER_NEAREST; // pixel art
    format.horizontalWrap = format.verticalWrap = plasma::TEXTURE_WRAP_NONE;
    m_icons[0] = m_engine->createTexture(SkillIconSize, SkillIconSize, SwimmingIconPixels, format);
    m_icons[1] = m_engine->createTexture(SkillIconSize, SkillIconSize, ClimbingIconPixels, format);
    m_icons[2] = m_engine->createTexture(SkillIconSize, SkillIconSize, RidingIconPixels, format);
}

int TravelSkillBarWidget::visibleRows() const
{
    int rows = 0;
    for (int i = 0; i < 3; ++i)
        rows += m_game->travelBarAlpha(i) > 0.0f;
    return rows;
}

void TravelSkillBarWidget::draw()
{
    const Creature *player = m_game->m_world->m_localCreature;
    if (!player || !player->m_playerData)
        return;
    static constexpr const wchar_t *Names[] = {L"Swimming", L"Climbing", L"Riding"};
    const float seconds[] = {player->m_playerData->swimmingSeconds, player->m_playerData->climbingSeconds,
                             player->m_playerData->ridingSeconds};
    // Each row: the icon, and beside it the name and level over the progress bar, both within the icon's height.
    // renderText places the text's baseline at its y.
    constexpr float Icon = 26.0f, Gap = 5.0f, BarWidth = 120.0f, BarHeight = 7.0f, TextBaseline = 12.0f;
    float y = 0.0f;
    for (int i = 0; i < 3; ++i) {
        const float alpha = m_game->travelBarAlpha(i);
        if (alpha <= 0.0f)
            continue;
        const int level = Creature::travelSkillLevel(seconds[i]);
        // Level n is reached after 6 n^2 seconds.
        float progress = 1.0f;
        if (level < Creature::TravelSkillCap) {
            const float from = 6.0f * level * level, to = 6.0f * (level + 1) * (level + 1);
            progress = std::min(1.0f, std::max(0.0f, (seconds[i] - from) / (to - from)));
        }
        renderImage(m_icons[i], 0.0f, y, Icon, Icon, alpha);
        std::wstringstream text;
        text << Names[i] << L" Lv " << level;
        setTextSize(13.0f);
        setTextStrokeRadius(3.0f);
        setTextStrokeColor(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, alpha));
        setTextColor(plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, alpha));
        const float textX = Icon + Gap;
        renderText(text.str(), textX, y + TextBaseline);
        const float barY = y + Icon - BarHeight - 2.0f;
        renderRectangle(textX - 1.0f, barY - 1.0f, BarWidth + 2.0f, BarHeight + 2.0f,
                        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.8f * alpha));
        renderRectangle(textX, barY, BarWidth, BarHeight, plasma::Vector<4, float>(0.2f, 0.2f, 0.25f, alpha));
        if (progress > 0.0f)
            renderRectangle(textX, barY, BarWidth * progress, BarHeight,
                            plasma::Vector<4, float>(0.45f, 0.8f, 1.0f, alpha));
        y += RowHeight;
    }
}

}

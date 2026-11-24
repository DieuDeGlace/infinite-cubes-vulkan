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

TravelSkillBarWidget::Row TravelSkillBarWidget::row(int skill) const
{
    Row row;
    const Creature *player = m_game->m_world->m_localCreature;
    if (!player || !player->m_playerData)
        return row;
    row.alpha = m_game->travelBarAlpha(skill);
    const float seconds[] = {player->m_playerData->swimmingSeconds, player->m_playerData->climbingSeconds,
                             player->m_playerData->ridingSeconds};
    row.level = Creature::travelSkillLevel(seconds[skill]);
    // Level n is reached after 6 n^2 seconds.
    row.progress = 1.0f;
    if (row.level < Creature::TravelSkillCap) {
        const float from = 6.0f * row.level * row.level, to = 6.0f * (row.level + 1) * (row.level + 1);
        row.progress = std::min(1.0f, std::max(0.0f, (seconds[skill] - from) / (to - from)));
    }
    return row;
}

void TravelSkillBarWidget::draw()
{
    // The icons, one row each, in the order Interface places the bars.
    float y = 0.0f;
    for (int i = 0; i < 3; ++i) {
        const float alpha = row(i).alpha;
        if (alpha <= 0.0f)
            continue;
        renderImage(m_icons[i], 0.0f, y + (RowHeight - IconSize) * 0.5f, IconSize, IconSize, alpha);
        y += RowHeight;
    }
}

}

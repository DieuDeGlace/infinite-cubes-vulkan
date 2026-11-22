// See cube/CharacterStyleWidget.h.
#include "plasma/TextIds.h"
#include <algorithm>
#include <sstream>
#include <vector>
#include "cube/widgets/CharacterStyleWidget.h"
#include "cube/Creature.h"
#include "cube/Game.h"
#include "cube/World.h"
#include "plasma/Display.h"
#include "plasma/Engine.h"
#include "plasma/Node.h"
#include "plasma/SmoothMeshShape.h"
#include "plasma/Transformation.h"
#include "plasma/Widget.h"
#include "cube/CreatureIds.h"
#include "cube/SpriteIds.h"
#include "plasma/EditFilter.h"

namespace cube {

namespace {

constexpr int PaletteX = 13, PaletteY = 197, CellSize = 17;
constexpr int Columns = 16, Shades = 7;

plasma::Vector<4, float> paletteColor(int column, int shade)
{
    static const plasma::Vector<4, float> colors[Columns] = {
        {1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 0.25f, 0.0f, 1.0f}, {1.0f, 0.5f, 0.0f, 1.0f}, {1.0f, 1.0f, 0.0f, 1.0f},
        {0.5f, 1.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 1.0f},  {0.0f, 1.0f, 0.5f, 1.0f}, {0.0f, 1.0f, 1.0f, 1.0f},
        {0.0f, 0.5f, 1.0f, 1.0f}, {0.0f, 0.25f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}, {0.25f, 0.0f, 1.0f, 1.0f},
        {0.5f, 0.0f, 1.0f, 1.0f}, {0.75f, 0.0f, 1.0f, 1.0f}, {1.0f, 0.0f, 1.0f, 1.0f}, {0.5f, 0.5f, 0.5f, 1.0f},
    };
    auto color = colors[column];
    if (shade < 3) {
        for (int c = 0; c < 3; ++c)
            color[c] *= shade * 0.25f + 0.2f;
    } else if (shade > 3) {
        color += (plasma::Vector<4, float>(1, 1, 1, 1) - color) * ((shade - 3) * 0.25f);
    }
    return color;
}

plasma::Vector<3, unsigned char> hairColor(const plasma::Vector<4, float> &color)
{
    return {(unsigned char)(color[0] * 255), (unsigned char)(color[1] * 255), (unsigned char)(color[2] * 255)};
}

using HairColor = plasma::Vector<3, unsigned char>;

plasma::Widget *makePalette(BaseWidget &parent, int top)
{
    auto *engine = parent.getEngine();
    auto *shape = engine->createSmoothMeshShape(L"");
    const float w = Columns * CellSize, h = Shades * CellSize;
    shape->m_vertexPositions.resize(4);
    shape->m_vertexPositions.value() = {{0, 0}, {w, 0}, {w, h}, {0, h}};
    shape->m_vertexColors.resize(4);
    shape->m_vertexColors.value().assign(4, {0, 0, 0, 0});
    shape->setFaces({{0, 1, 2, 3}}, true);
    shape->setSubdivisions(0, true);
    shape->attributesChanged(true);
    auto *node = engine->createNode(nullptr, shape, nullptr, parent.getNode(), L"");
    node->transformation()->m_translation.value() = {PaletteX, float(top)};
    node->transformation()->attributesChanged(true);
    return engine->createWidget(node, L"");
}

bool pointedColor(BaseWidget &widget, int top, HairColor &color)
{
    const auto p = widget.getPointerOffset();
    const float x = p[0] - PaletteX, y = p[1] - top;
    if (x < 0 || y < 0 || x >= Columns * CellSize || y >= Shades * CellSize)
        return false;
    color = hairColor(paletteColor(int(x) / CellSize, int(y) / CellSize));
    return true;
}

void drawPalette(BaseWidget &widget, int top, const HairColor &selected)
{
    auto *engine = widget.getEngine();
    const auto origin =
        widget.getNode()->transformation()->m_pivot.value() + plasma::Vector<2, float>(PaletteX, float(top));
    engine->fillRectangle(origin, {Columns * CellSize, Shades * CellSize}, {0, 0, 0, 1});
    for (int column = 0; column < Columns; ++column) {
        for (int shade = 0; shade < Shades; ++shade) {
            const auto swatch = paletteColor(column, shade);
            const auto at = origin + plasma::Vector<2, float>(float(column * CellSize), float(shade * CellSize));
            if (hairColor(swatch) == selected)
                engine->fillRectangle(at, {CellSize, CellSize}, {1, 1, 1, 1});
            engine->fillRectangle(at + plasma::Vector<2, float>(1, 1), {CellSize - 2, CellSize - 2}, swatch);
        }
    }
}

}

CharacterStyleWidget::CharacterStyleWidget(Game *game, plasma::Node *node, plasma::Node *leftButton,
                                           plasma::Node *rightButton)
    : BaseWidget(game->m_engine, node, L""),
      m_game(game),
      m_race(0),
      m_classType(0),
      m_gender(0),
      m_face(0),
      m_haircut(0),
      m_currentHairColor(0xff, 0xc8, 0x32),
      m_previewHairColor(0xff, 0xff, 0xff)
{
    // Each row has its own copies of the two button nodes, with their own transformation and display; the "leftbutton"
    // / "rightbutton" widget inside each copy calls the row's handler when pressed.
    m_raceLeftButton = leftButton->instance(node);
    m_raceLeftButton->setTransformation(leftButton->transformation()->clone());
    m_raceLeftButton->setDisplay(leftButton->display()->clone());
    m_raceLeftButton->connectWidget(L"leftbutton", plasma::WIDGET_EVENT_POINTER_PRESSED, this,
                                    &CharacterStyleWidget::decreaseRace, true);
    m_raceRightButton = rightButton->instance(node);
    m_raceRightButton->setTransformation(rightButton->transformation()->clone());
    m_raceRightButton->setDisplay(rightButton->display()->clone());
    m_raceRightButton->connectWidget(L"rightbutton", plasma::WIDGET_EVENT_POINTER_PRESSED, this,
                                     &CharacterStyleWidget::increaseRace, true);
    m_classLeftButton = leftButton->instance(node);
    m_classLeftButton->setTransformation(leftButton->transformation()->clone());
    m_classLeftButton->setDisplay(leftButton->display()->clone());
    m_classLeftButton->connectWidget(L"leftbutton", plasma::WIDGET_EVENT_POINTER_PRESSED, this,
                                     &CharacterStyleWidget::decreaseClass, true);
    m_classRightButton = rightButton->instance(node);
    m_classRightButton->setTransformation(rightButton->transformation()->clone());
    m_classRightButton->setDisplay(rightButton->display()->clone());
    m_classRightButton->connectWidget(L"rightbutton", plasma::WIDGET_EVENT_POINTER_PRESSED, this,
                                      &CharacterStyleWidget::increaseClass, true);
    m_genderLeftButton = leftButton->instance(node);
    m_genderLeftButton->setTransformation(leftButton->transformation()->clone());
    m_genderLeftButton->setDisplay(leftButton->display()->clone());
    m_genderLeftButton->connectWidget(L"leftbutton", plasma::WIDGET_EVENT_POINTER_PRESSED, this,
                                      &CharacterStyleWidget::decreaseGender, true);
    m_genderRightButton = rightButton->instance(node);
    m_genderRightButton->setTransformation(rightButton->transformation()->clone());
    m_genderRightButton->setDisplay(rightButton->display()->clone());
    m_genderRightButton->connectWidget(L"rightbutton", plasma::WIDGET_EVENT_POINTER_PRESSED, this,
                                       &CharacterStyleWidget::increaseGender, true);
    m_faceLeftButton = leftButton->instance(node);
    m_faceLeftButton->setTransformation(leftButton->transformation()->clone());
    m_faceLeftButton->setDisplay(leftButton->display()->clone());
    m_faceLeftButton->connectWidget(L"leftbutton", plasma::WIDGET_EVENT_POINTER_PRESSED, this,
                                    &CharacterStyleWidget::decreaseFace, true);
    m_faceRightButton = rightButton->instance(node);
    m_faceRightButton->setTransformation(rightButton->transformation()->clone());
    m_faceRightButton->setDisplay(rightButton->display()->clone());
    m_faceRightButton->connectWidget(L"rightbutton", plasma::WIDGET_EVENT_POINTER_PRESSED, this,
                                     &CharacterStyleWidget::increaseFace, true);
    m_haircutLeftButton = leftButton->instance(node);
    m_haircutLeftButton->setTransformation(leftButton->transformation()->clone());
    m_haircutLeftButton->setDisplay(leftButton->display()->clone());
    m_haircutLeftButton->connectWidget(L"leftbutton", plasma::WIDGET_EVENT_POINTER_PRESSED, this,
                                       &CharacterStyleWidget::decreaseHaircut, true);
    m_haircutRightButton = rightButton->instance(node);
    m_haircutRightButton->setTransformation(rightButton->transformation()->clone());
    m_haircutRightButton->setDisplay(rightButton->display()->clone());
    m_haircutRightButton->connectWidget(L"rightbutton", plasma::WIDGET_EVENT_POINTER_PRESSED, this,
                                        &CharacterStyleWidget::increaseHaircut, true);

    makePalette(*this, PaletteY)
        ->connect(plasma::WIDGET_EVENT_POINTER_PRESSED, this, &CharacterStyleWidget::selectHairColor, true, true);
    setFont(L"resource1.dat");
}

void CharacterStyleWidget::draw()
{
    std::wstringstream text;
    switch (m_race) {
    case CharacterRaceChoiceHuman:
        text << L"Human";
        break;
    case CharacterRaceChoiceElf:
        text << L"Elf";
        break;
    case CharacterRaceChoiceDwarf:
        text << L"Dwarf";
        break;
    case CharacterRaceChoiceOrc:
        text << L"Orc";
        break;
    case CharacterRaceChoiceGoblin:
        text << L"Goblin";
        break;
    case CharacterRaceChoiceLizard:
        text << L"Lizard";
        break;
    case CharacterRaceChoiceUndead:
        text << L"Undead";
        break;
    case CharacterRaceChoiceFrogman:
        text << L"Frogman";
        break;
    }
    setTextSize(12.0f);
    setTextStrokeRadius(3.0f);
    setTextStrokeColor(plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f));
    setTextColor(plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));

    // Each row: its label at the left, the choice centred between the buttons (1 << plasma::TEXT_CENTER).
    setTextFlags(0);
    renderText(L"Race", 15.0f, 27.0f);
    setTextFlags(1 << plasma::TEXT_CENTER);
    renderText(text.str(), (getContentWidth() - 110.0f) * 0.5f + 100.0f, 27.0f);
    text.str(L"");
    if (m_gender)
        text << "Female";
    else
        text << "Male";
    setTextFlags(0);
    renderText(L"Gender", 15.0f, 57.0f);
    setTextFlags(1 << plasma::TEXT_CENTER);
    renderText(text.str(), (getContentWidth() - 110.0f) * 0.5f + 100.0f, 57.0f);
    text.str(L"");
    switch (m_classType) {
    case CharacterClassChoiceWarrior:
        text << L"Warrior";
        break;
    case CharacterClassChoiceRanger:
        text << L"Ranger";
        break;
    case CharacterClassChoiceMage:
        text << L"Mage";
        break;
    case CharacterClassChoiceRogue:
        text << L"Rogue";
        break;
    }
    setTextFlags(0);
    renderText(L"Class", 15.0f, 87.0f);
    setTextFlags(1 << plasma::TEXT_CENTER);
    renderText(text.str(), (getContentWidth() - 110.0f) * 0.5f + 100.0f, 87.0f);
    text.str(L"");
    text << L"Face " << m_face + 1;
    setTextFlags(0);
    renderText(L"Face", 15.0f, 117.0f);
    setTextFlags(1 << plasma::TEXT_CENTER);
    renderText(text.str(), (getContentWidth() - 110.0f) * 0.5f + 100.0f, 117.0f);
    text.str(L"");
    text << L"Haircut " << m_haircut + 1;
    setTextFlags(0);
    renderText(L"Haircut", 15.0f, 147.0f);
    setTextFlags(1 << plasma::TEXT_CENTER);
    renderText(text.str(), (getContentWidth() - 110.0f) * 0.5f + 100.0f, 147.0f);

    // A button under the pointer is drawn cyan, the others white.
    m_raceLeftButton->display()->m_fillColor.value() = m_raceLeftButton->widget()->isPointerOver()
                                                           ? plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f)
                                                           : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f);
    m_raceRightButton->display()->m_fillColor.value() = m_raceRightButton->widget()->isPointerOver()
                                                            ? plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f)
                                                            : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f);
    m_genderLeftButton->display()->m_fillColor.value() = m_genderLeftButton->widget()->isPointerOver()
                                                             ? plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f)
                                                             : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f);
    m_genderRightButton->display()->m_fillColor.value() = m_genderRightButton->widget()->isPointerOver()
                                                              ? plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f)
                                                              : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f);
    m_classLeftButton->display()->m_fillColor.value() = m_classLeftButton->widget()->isPointerOver()
                                                            ? plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f)
                                                            : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f);
    m_classRightButton->display()->m_fillColor.value() = m_classRightButton->widget()->isPointerOver()
                                                             ? plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f)
                                                             : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f);
    m_faceLeftButton->display()->m_fillColor.value() = m_faceLeftButton->widget()->isPointerOver()
                                                           ? plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f)
                                                           : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f);
    m_faceRightButton->display()->m_fillColor.value() = m_faceRightButton->widget()->isPointerOver()
                                                            ? plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f)
                                                            : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f);
    m_haircutLeftButton->display()->m_fillColor.value() = m_haircutLeftButton->widget()->isPointerOver()
                                                              ? plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f)
                                                              : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f);
    m_haircutRightButton->display()->m_fillColor.value() = m_haircutRightButton->widget()->isPointerOver()
                                                               ? plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f)
                                                               : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f);

    setTextFlags(1 << plasma::TEXT_CENTER);
    renderText(L"Hair color", getContentWidth() * 0.5f, 182.0f);
    drawPalette(*this, PaletteY, m_currentHairColor);

    HairColor preview = m_currentHairColor;
    pointedColor(*this, PaletteY, preview);
    if (m_previewHairColor != preview) {
        m_previewHairColor = preview;
        updateCreature(true);
    }
}

void CharacterStyleWidget::decreaseClass()
{
    m_game->playSound(SoundMenuSelect);
    if (--m_classType < CharacterClassChoiceWarrior)
        m_classType = CharacterClassChoiceRogue;
    updateCreature(false);
}

void CharacterStyleWidget::increaseClass()
{
    m_game->playSound(SoundMenuSelect);
    if (++m_classType > CharacterClassChoiceRogue)
        m_classType = CharacterClassChoiceWarrior;
    updateCreature(false);
}

void CharacterStyleWidget::selectHairColor()
{
    if (pointedColor(*this, PaletteY, m_currentHairColor)) {
        m_game->playSound(SoundMenuSelect);
        commitHairColors();
    }
}

void CharacterStyleWidget::commitHairColors()
{
    m_previewHairColor = m_currentHairColor;
    m_previewTipColor = m_gradient.tips;
    updateCreature(true);
}

SecondHairWidget::SecondHairWidget(CharacterStyleWidget &style, plasma::Node *node, plasma::Node *left,
                                   plasma::Node *right)
    : BaseWidget(style.getEngine(), node, L""),
      m_style(style)
{
    using Handler = void (SecondHairWidget::*)();
    const Handler decrease[] = {&SecondHairWidget::toggle, &SecondHairWidget::earlierStart,
                                &SecondHairWidget::narrowerBlend};
    const Handler increase[] = {&SecondHairWidget::toggle, &SecondHairWidget::laterStart,
                                &SecondHairWidget::widerBlend};
    auto arrow = [&](plasma::Node *button, const wchar_t *name, Handler handler) {
        auto *copy = button->instance(node);
        copy->makeTransformationUnique();
        copy->makeDisplayUnique();
        copy->connectWidget(name, plasma::WIDGET_EVENT_POINTER_PRESSED, this, handler, true);
        return copy;
    };
    for (size_t i = 0; i < m_left.size(); ++i) {
        m_left[i] = arrow(left, L"leftbutton", decrease[i]);
        m_right[i] = arrow(right, L"rightbutton", increase[i]);
    }
    makePalette(*this, PaletteY)
        ->connect(plasma::WIDGET_EVENT_POINTER_PRESSED, this, &SecondHairWidget::selectColor, true, true);
    setFont(L"resource1.dat");
    setTextStrokeRadius(3.0f);
    setTextStrokeColor({0, 0, 0, 1});
}

void SecondHairWidget::resized()
{
    for (size_t i = 0; i < m_left.size(); ++i) {
        const float y = 42.0f + 30.0f * i;
        m_left[i]->widget()->moveTo(100.0f, y, true);
        auto *right = m_right[i]->widget();
        right->moveTo(viewWidth() - right->viewWidth() - 15.0f, y, true);
    }
}

void SecondHairWidget::draw()
{
    const auto &gradient = m_style.m_gradient;
    setTextFlags(0);
    renderText(L"Dual-tone hair", 15.0f, 27.0f);
    const wchar_t *labels[] = {L"Enabled", L"Start", L"Blend"};
    const std::wstring values[] = {gradient.enabled ? L"On" : L"Off", std::to_wstring(gradient.start) + L"%",
                                   std::to_wstring(gradient.blend) + L"%"};
    for (size_t i = 0; i < m_left.size(); ++i) {
        const float y = 57.0f + 30.0f * i;
        setTextFlags(0);
        renderText(labels[i], 15.0f, y);
        setTextFlags(1 << plasma::TEXT_CENTER);
        renderText(values[i], (getContentWidth() - 110.0f) * 0.5f + 100.0f, y);
    }
    renderText(L"Second color", getContentWidth() * 0.5f, 182.0f);
    drawPalette(*this, PaletteY, gradient.tips);
    HairColor preview = gradient.tips;
    if (gradient.enabled)
        pointedColor(*this, PaletteY, preview);
    if (preview != m_style.m_previewTipColor) {
        m_style.m_previewTipColor = preview;
        m_style.updateCreature(true);
    }
}

void SecondHairWidget::selectColor()
{
    if (pointedColor(*this, PaletteY, m_style.m_gradient.tips)) {
        m_style.m_gradient.enabled = true;
        m_style.m_game->playSound(SoundMenuSelect);
        m_style.commitHairColors();
    }
}

void SecondHairWidget::toggle()
{
    m_style.m_gradient.enabled = !m_style.m_gradient.enabled;
    m_style.m_game->playSound(SoundMenuSelect);
    m_style.commitHairColors();
}

void SecondHairWidget::adjustPercent(unsigned char &value, int step)
{
    value = (unsigned char)std::clamp(int(value) + step, 0, 100);
    m_style.m_game->playSound(SoundMenuSelect);
    m_style.commitHairColors();
}

void SecondHairWidget::earlierStart()
{
    adjustPercent(m_style.m_gradient.start, -5);
}
void SecondHairWidget::laterStart()
{
    adjustPercent(m_style.m_gradient.start, 5);
}
void SecondHairWidget::narrowerBlend()
{
    adjustPercent(m_style.m_gradient.blend, -5);
}
void SecondHairWidget::widerBlend()
{
    adjustPercent(m_style.m_gradient.blend, 5);
}

void CharacterStyleWidget::decreaseFace()
{
    m_game->playSound(SoundMenuSelect);
    m_face--;
    updateCreature(false);
}

void CharacterStyleWidget::increaseFace()
{
    m_game->playSound(SoundMenuSelect);
    m_face++;
    updateCreature(false);
}

void CharacterStyleWidget::decreaseGender()
{
    m_game->playSound(SoundMenuSelect);
    m_gender = (m_gender + 1) % 2;
    updateCreature(false);
}

// The same code as decreaseGender.
void CharacterStyleWidget::increaseGender()
{
    m_game->playSound(SoundMenuSelect);
    m_gender = (m_gender + 1) % 2;
    updateCreature(false);
}

void CharacterStyleWidget::decreaseHaircut()
{
    m_game->playSound(SoundMenuSelect);
    m_haircut--;
    updateCreature(false);
}

void CharacterStyleWidget::increaseHaircut()
{
    m_game->playSound(SoundMenuSelect);
    m_haircut++;
    updateCreature(false);
}

void CharacterStyleWidget::decreaseRace()
{
    m_game->playSound(SoundMenuSelect);
    if (--m_race < CharacterRaceChoiceHuman)
        m_race = CharacterRaceChoiceFrogman;
    updateCreature(false);
}

void CharacterStyleWidget::increaseRace()
{
    m_game->playSound(SoundMenuSelect);
    if (++m_race > CharacterRaceChoiceFrogman)
        m_race = CharacterRaceChoiceHuman;
    updateCreature(false);
}

void CharacterStyleWidget::resized()
{
    // Five rows 30 apart: the left button at x 100, the right one 15 from the right edge.
    m_raceLeftButton->widget()->moveTo(100.0f, 12.0f, true);
    plasma::Widget *button = m_raceRightButton->widget();
    button->moveTo(viewWidth() - button->viewWidth() - 15.0f, 12.0f, true);
    m_genderLeftButton->widget()->moveTo(100.0f, 42.0f, true);
    button = m_genderRightButton->widget();
    button->moveTo(viewWidth() - button->viewWidth() - 15.0f, 42.0f, true);
    m_classLeftButton->widget()->moveTo(100.0f, 72.0f, true);
    button = m_classRightButton->widget();
    button->moveTo(viewWidth() - button->viewWidth() - 15.0f, 72.0f, true);
    m_faceLeftButton->widget()->moveTo(100.0f, 102.0f, true);
    button = m_faceRightButton->widget();
    button->moveTo(viewWidth() - button->viewWidth() - 15.0f, 102.0f, true);
    m_haircutLeftButton->widget()->moveTo(100.0f, 132.0f, true);
    button = m_haircutRightButton->widget();
    button->moveTo(viewWidth() - button->viewWidth() - 15.0f, 132.0f, true);
}

void CharacterStyleWidget::reset()
{
    m_classType = CharacterClassChoiceWarrior;
    m_gender = 0;
    m_race = CharacterRaceChoiceHuman;
    m_face = 0;
    m_haircut = 0;
    m_currentHairColor = plasma::Vector<3, unsigned char>(0xff, 0xc8, 0x32);
    m_previewHairColor = m_currentHairColor;
    m_gradient = {};
    m_previewTipColor = m_gradient.tips;
}

void CharacterStyleWidget::updateCreature(bool preview)
{
    m_game->m_world->enterZoneMeshes();
    Creature *creature = m_game->m_world->m_localCreature;
    auto &player = *creature->m_playerData;
    player.hairColor = m_previewHairColor;
    player.hairGradient = m_gradient;
    player.hairGradient.tips = m_previewTipColor;
    if (preview) {
        // Frogman markings retain their native tint in single-color mode.
        if (m_race != CharacterRaceChoiceFrogman)
            creature->m_entityData.appearance.hairColor = player.hairColor;
        m_game->m_world->leaveZoneMeshes();
        return;
    }

    // The creature race of the chosen race and gender (male, female).
    switch (m_race) {
    case CharacterRaceChoiceHuman:
        creature->m_entityData.race = m_gender + RaceHumanMale;
        break;
    case CharacterRaceChoiceElf:
        creature->m_entityData.race = m_gender;
        break;
    case CharacterRaceChoiceDwarf:
        creature->m_entityData.race = m_gender + RaceDwarfMale;
        break;
    case CharacterRaceChoiceOrc:
        creature->m_entityData.race = m_gender + RaceOrcMale;
        break;
    case CharacterRaceChoiceGoblin:
        creature->m_entityData.race = m_gender + RaceGoblinMale;
        break;
    case CharacterRaceChoiceLizard:
        creature->m_entityData.race = m_gender + RaceLizardmanMale;
        break;
    case CharacterRaceChoiceUndead:
        creature->m_entityData.race = m_gender + RaceUndeadMale;
        break;
    case CharacterRaceChoiceFrogman:
        creature->m_entityData.race = m_gender + RaceFrogmanMale;
        break;
    }
    creature->m_entityData.classType = m_classType + 1;

    // The model ranges of the creature race: faces firstFace..lastFace, haircuts firstHaircut..lastHaircut; the
    // choices wrap around them.
    int firstFace = SpriteHumanHeadM01;
    int lastFace = SpriteHumanHeadM06;
    int firstHaircut = SpriteHumanHairM01;
    int lastHaircut = SpriteHumanHairM15;
    switch (creature->m_entityData.race) {
    case RaceElfMale:
        firstFace = SpriteElfHeadM01;
        lastFace = SpriteElfHeadM04;
        firstHaircut = SpriteElfHairM01;
        lastHaircut = SpriteElfHairM10;
        break;
    case RaceElfFemale:
        firstFace = SpriteElfHeadF01;
        lastFace = SpriteElfHeadF06;
        firstHaircut = SpriteElfHairF01;
        lastHaircut = SpriteElfHairF11;
        break;
    case RaceHumanFemale:
        firstFace = SpriteHumanHeadF01;
        lastFace = SpriteHumanHeadF06;
        firstHaircut = SpriteHumanHairF01;
        lastHaircut = SpriteHumanHairF09;
        break;
    case RaceDwarfMale:
        firstFace = SpriteDwarfHeadM01;
        lastFace = SpriteDwarfHeadM05;
        firstHaircut = SpriteDwarfHairM01;
        lastHaircut = SpriteDwarfHairM03;
        break;
    case RaceDwarfFemale:
        firstFace = SpriteDwarfHeadF01;
        lastFace = SpriteDwarfHeadF05;
        firstHaircut = SpriteDwarfHairF01;
        lastHaircut = SpriteDwarfHairF06;
        break;
    case RaceGoblinMale:
        firstFace = SpriteGoblinHeadM01;
        lastFace = SpriteGoblinHeadM05;
        firstHaircut = SpriteGoblinHairM01;
        lastHaircut = SpriteGoblinHairM06;
        break;
    case RaceGoblinFemale:
        firstFace = SpriteGoblinHeadF01;
        lastFace = SpriteGoblinHeadF05;
        firstHaircut = SpriteGoblinHairF01;
        lastHaircut = SpriteGoblinHairF06;
        break;
    case RaceOrcMale:
        firstFace = SpriteOrcHeadM01;
        lastFace = SpriteOrcHeadM05;
        firstHaircut = SpriteOrcHairM01;
        lastHaircut = SpriteOrcHairM08;
        break;
    case RaceOrcFemale:
        firstFace = SpriteOrcHeadF01;
        lastFace = SpriteOrcHeadF05;
        firstHaircut = SpriteOrcHairF01;
        lastHaircut = SpriteOrcHairF04;
        break;
    case RaceLizardmanMale:
        firstFace = SpriteLizardHeadM01;
        lastFace = SpriteLizardHeadM02;
        firstHaircut = SpriteLizardHairM01;
        lastHaircut = SpriteLizardHairM06;
        break;
    case RaceLizardmanFemale:
        firstFace = SpriteLizardHeadF01;
        lastFace = SpriteLizardHeadF05;
        firstHaircut = SpriteLizardHairM01;
        lastHaircut = SpriteLizardHairM06;
        break;
    case RaceUndeadMale:
        firstFace = SpriteUndeadHeadM01;
        lastFace = SpriteUndeadHeadM06;
        firstHaircut = SpriteUndeadHairM01;
        lastHaircut = SpriteUndeadHairM06;
        break;
    case RaceUndeadFemale:
        firstFace = SpriteUndeadHeadF01;
        lastFace = SpriteUndeadHeadF06;
        firstHaircut = SpriteUndeadHairF01;
        lastHaircut = SpriteUndeadHairF06;
        break;
    case RaceFrogmanMale:
        firstFace = SpriteFrogmanHeadM01;
        lastFace = SpriteFrogmanHeadM05;
        firstHaircut = SpriteFrogmanHairM01;
        lastHaircut = SpriteFrogmanHairM05;
        break;
    case RaceFrogmanFemale:
        firstFace = SpriteFrogmanHeadF01;
        lastFace = SpriteFrogmanHeadF04;
        firstHaircut = SpriteFrogmanHairF01;
        lastHaircut = SpriteFrogmanHairF04;
        break;
    }
    int faces = lastFace - firstFace;
    if (m_face < 0)
        m_face = faces;
    if (m_face >= faces + 1)
        m_face = 0;
    if (firstHaircut >= 0) {
        int haircuts = lastHaircut - firstHaircut;
        if (m_haircut < 0)
            m_haircut = haircuts;
        if (m_haircut >= haircuts + 1)
            m_haircut = 0;
    }

    player.face = m_face;
    player.haircut = m_haircut;
    player.race = m_race;
    player.gender = m_gender;
    creature->m_entityData.appearance = Creature::EntityData::Appearance();
    fillAppearance(creature->m_entityData.race, creature->m_entityData.appearance, creature->m_playerData, false);
    m_game->equipNewCharacter();
    m_game->m_world->leaveZoneMeshes();
}

}

// On-screen controls for the game: a movement stick on the left, a camera drag area on the right and buttons bound
// to the game's control actions. Menus are operated by touching them directly and keep only the menu buttons.
#pragma once

#include <algorithm>
#include <array>
#include <glm/glm.hpp>
#include "cube/Controls.h"

namespace android {

struct TouchButton {
    enum Corner {
        TopLeft,
        TopRight,
        BottomRight
    };

    int control;          // cube::Controls::Action
    Corner corner;        // the screen corner the offset is measured from
    glm::vec2 offset;     // centre, in screen heights inward from the corner
    float radius;         // in screen heights
    bool inMenus;         // stays available while a menu is open
    const wchar_t *label; // drawn in the game's interface font
};

constexpr std::array<TouchButton, 16> TouchButtons = {{
    {cube::Controls::ButtonAttack, TouchButton::BottomRight, {0.17f, 0.20f}, 0.085f, false, L"ATTACK"},
    {cube::Controls::ButtonSpecialAttack, TouchButton::BottomRight, {0.36f, 0.12f}, 0.060f, false, L"SPECIAL"},
    {cube::Controls::ButtonJump, TouchButton::BottomRight, {0.11f, 0.43f}, 0.060f, false, L"JUMP"},
    {cube::Controls::ButtonDodge, TouchButton::BottomRight, {0.31f, 0.33f}, 0.055f, false, L"DODGE"},
    {cube::Controls::ButtonUse, TouchButton::BottomRight, {0.50f, 0.28f}, 0.050f, false, L"USE"},
    {cube::Controls::ButtonClassSkill, TouchButton::BottomRight, {0.54f, 0.10f}, 0.050f, false, L"SKILL"},
    {cube::Controls::ButtonUltimateSkill, TouchButton::BottomRight, {0.70f, 0.10f}, 0.050f, false, L"ULTIMATE"},
    {cube::Controls::ButtonQuickItem, TouchButton::BottomRight, {0.25f, 0.54f}, 0.045f, false, L"ITEM"},
    {cube::Controls::ButtonSystemMenu, TouchButton::TopRight, {0.07f, 0.07f}, 0.040f, true, L"MENU"},
    {cube::Controls::ButtonInventory, TouchButton::TopRight, {0.17f, 0.07f}, 0.040f, true, L"BAG"},
    {cube::Controls::ButtonCraftingMenu, TouchButton::TopRight, {0.27f, 0.07f}, 0.040f, true, L"CRAFT"},
    {cube::Controls::ButtonWorldMap, TouchButton::TopRight, {0.37f, 0.07f}, 0.040f, true, L"MAP"},
    {cube::Controls::ButtonLamp, TouchButton::TopRight, {0.47f, 0.07f}, 0.040f, false, L"LAMP"},
    {cube::Controls::ButtonCallPet, TouchButton::TopRight, {0.57f, 0.07f}, 0.040f, false, L"PET"},
    {cube::Controls::ButtonZoomIn, TouchButton::TopLeft, {0.07f, 0.07f}, 0.040f, true, L"ZOOM +"},
    {cube::Controls::ButtonZoomOut, TouchButton::TopLeft, {0.17f, 0.07f}, 0.040f, true, L"ZOOM -"},
}};

class GameTouch {
public:
    void resize(glm::vec2 size)
    {
        if (size != m_size) {
            cancel();
            m_size = size;
        }
    }

    glm::vec2 center(const TouchButton &button) const
    {
        const glm::vec2 offset = button.offset * m_size.y;
        const float x = button.corner == TouchButton::TopLeft ? offset.x : m_size.x - offset.x;
        const float y = button.corner == TouchButton::BottomRight ? m_size.y - offset.y : offset.y;
        return {x, y};
    }
    float radius(const TouchButton &button) const { return button.radius * m_size.y; }

    // Takes the touch for a button, the stick or the camera. False when a menu should receive it instead.
    bool press(int id, glm::vec2 position, bool inMenu)
    {
        for (size_t i = 0; i < TouchButtons.size(); ++i) {
            const TouchButton &button = TouchButtons[i];
            // Fingers are less precise than the drawn circle.
            const float reach = radius(button) * 1.25f;
            if ((inMenu && !button.inMenus) || m_buttonIds[i] >= 0 || glm::distance(position, center(button)) > reach)
                continue;
            m_buttonIds[i] = id;
            return true;
        }
        if (inMenu)
            return false;
        if (position.x < m_size.x * 0.45f) {
            if (m_stickId < 0) {
                m_stickId = id;
                m_stickCenter = position;
            }
        } else if (m_lookId < 0) {
            m_lookId = id;
            m_lastLook = position;
        }
        return true;
    }

    void move(int id, glm::vec2 position)
    {
        if (id == m_stickId) {
            constexpr float DeadZone = 0.13f;
            const glm::vec2 offset = (position - m_stickCenter) / stickRadius();
            const float length = glm::length(offset);
            m_stick = length > DeadZone ? offset / length * std::min((length - DeadZone) / (1.0f - DeadZone), 1.0f)
                                        : glm::vec2(0.0f);
        } else if (id == m_lookId) {
            m_look += position - m_lastLook;
            m_lastLook = position;
        }
    }

    void release(int id)
    {
        std::replace(m_buttonIds.begin(), m_buttonIds.end(), id, -1);
        if (id == m_stickId) {
            m_stickId = -1;
            m_stick = glm::vec2(0.0f);
        }
        if (id == m_lookId)
            m_lookId = -1;
    }

    void cancel()
    {
        m_buttonIds.fill(-1);
        m_stickId = m_lookId = -1;
        m_stick = m_look = glm::vec2(0.0f);
    }

    bool pressed(size_t button) const { return m_buttonIds[button] >= 0; }
    // The stick's deflection, each axis in -1..1 with down and right positive.
    glm::vec2 stick() const { return m_stick; }
    // The camera drag in pixels since the last call.
    glm::vec2 takeLook()
    {
        const glm::vec2 look = m_look;
        m_look = glm::vec2(0.0f);
        return look;
    }

    float stickRadius() const { return std::max(1.0f, m_size.y * 0.11f); }
    glm::vec2 stickCenter() const { return m_stickId >= 0 ? m_stickCenter : m_size * glm::vec2(0.14f, 0.76f); }
    glm::vec2 stickThumb() const { return stickCenter() + m_stick * stickRadius(); }

private:
    glm::vec2 m_size {0.0f};
    std::array<int, TouchButtons.size()> m_buttonIds = filled();
    int m_stickId = -1;
    int m_lookId = -1;
    glm::vec2 m_stickCenter {0.0f};
    glm::vec2 m_stick {0.0f};
    glm::vec2 m_lastLook {0.0f};
    glm::vec2 m_look {0.0f};

    static constexpr std::array<int, TouchButtons.size()> filled()
    {
        std::array<int, TouchButtons.size()> ids {};
        for (int &id : ids)
            id = -1;
        return ids;
    }
};

}

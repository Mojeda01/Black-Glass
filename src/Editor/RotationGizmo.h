#pragma once
#include "EditorTypes.h"
#include "../Debug/DebugDraw.h"

#include <glm/glm.hpp>

#include <string>

struct RotationTarget
{
    bool valid = false;
    glm::vec3 center{ 0.0f };
    float radius = 1.0f;
};

class RotationGizmo
{
public:
    bool dragging() const { return m_dragging; }
    bool hovering() const { return m_hover; }

    bool onPress();
    void onRelease();

    bool update(const ViewInfo& view, const RotationTarget& target, bool hasRay,
                const glm::vec3& origin, const glm::vec3& direction, bool cursorFree,
                float snapDegrees, bool visible, DebugDraw& debug, float& stepDegrees,
                std::string& label);

private:
    bool m_hover = false;
    bool m_dragging = false;
    float m_lastAngle = 0.0f;
    float m_startAngle = 0.0f;
    float m_accumulated = 0.0f;
    float m_applied = 0.0f;
    RotationTarget m_target;
};

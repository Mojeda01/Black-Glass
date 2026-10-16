#pragma once
#include "EditorTypes.h"
#include "../Debug/DebugDraw.h"

#include <glm/glm.hpp>

#include <string>

struct TranslateTarget
{
    bool valid = false;
    glm::vec3 center{ 0.0f };
};

class TranslateGizmo
{
public:
    bool dragging() const { return m_dragging; }
    bool hovering() const { return m_handle != Handle::None && !m_dragging; }

    bool onPress();
    void onRelease();

    bool update(const ViewInfo& view, const TranslateTarget& target, bool hasRay,
                const glm::vec3& origin, const glm::vec3& direction, bool cursorFree,
                float snapStep, bool visible, DebugDraw& debug, glm::vec3& step,
                std::string& label);

private:
    enum class Handle
    {
        None,
        AxisX,
        AxisY,
        AxisZ,
        PlaneXZ
    };

    bool dragPoint(Handle handle, const glm::vec3& origin, const glm::vec3& direction, glm::vec3& out) const;

    Handle m_handle = Handle::None;
    bool m_dragging = false;
    bool m_started = false;
    glm::vec3 m_anchor{ 0.0f };
    glm::vec3 m_startPoint{ 0.0f };
    glm::vec3 m_applied{ 0.0f };
    TranslateTarget m_target;
};

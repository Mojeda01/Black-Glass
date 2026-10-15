#pragma once
#include "EditorTypes.h"
#include "../Debug/DebugDraw.h"

#include <glm/glm.hpp>

#include <string>

struct HandleTarget
{
    bool valid = false;
    glm::vec3 position{ 0.0f };
    float yawDegrees = 0.0f;
    glm::vec3 size{ 1.0f };
};

struct HandleEdit
{
    glm::vec3 position{ 0.0f };
    glm::vec3 size{ 1.0f };
};

class BrushHandles
{
public:
    bool dragging() const { return m_dragging; }
    bool hovering() const { return m_hover >= 0; }

    bool onPress();
    void onRelease();

    bool update(const ViewInfo& view, const HandleTarget& target, bool cursorFree, float grid,
                const SnapSets& snaps, bool visible, DebugDraw& debug, HandleEdit& edit, std::string& label);

private:
    int m_hover = -1;
    bool m_dragging = false;

    ViewInfo m_view;
    HandleTarget m_target;

    int m_face = 0;
    glm::vec3 m_normal{ 0.0f };
    glm::vec3 m_faceStart{ 0.0f };
    glm::vec3 m_sizeStart{ 1.0f };
    glm::vec3 m_positionStart{ 0.0f };
    glm::vec2 m_mouseStart{ 0.0f };
    glm::vec2 m_screenDirection{ 0.0f };
};

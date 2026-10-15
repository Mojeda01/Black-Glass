#pragma once
#include "Brush.h"
#include "EditorTypes.h"
#include "../Debug/DebugDraw.h"

#include <glm/glm.hpp>

#include <string>

struct BrushBounds
{
    glm::vec3 center{ 0.0f };
    glm::vec3 size{ 1.0f };
    float yawDegrees = 0.0f;
};

class BrushTool
{
public:
    void arm();
    void disarm();
    bool armed() const { return m_armed; }

    bool cancel();
    bool onPress();
    void onRelease();

    bool update(const ViewInfo& view, bool hasRay, const glm::vec3& origin, const glm::vec3& direction,
                bool cursorFree, const SurfaceHit& surface, const SnapSets& snaps, float grid,
                bool visible, const BrushDef& def, DebugDraw& debug, BrushBounds& out, std::string& label);

private:
    enum class Phase
    {
        Hover,
        Footprint,
        Height
    };

    BrushBounds currentBounds(float height) const;
    float directionYaw() const;
    void drawPreview(DebugDraw& debug, float height) const;

    bool m_armed = false;
    Phase m_phase = Phase::Hover;
    bool m_hasHover = false;
    bool m_confirm = false;
    bool m_heightReady = false;
    glm::vec2 m_hover{ 0.0f };
    float m_hoverBase = 0.0f;
    glm::vec2 m_start{ 0.0f };
    glm::vec2 m_end{ 0.0f };
    float m_baseY = 0.0f;
    float m_height = 1.0f;
    float m_heightStart = 1.0f;
    float m_cursorStartY = 0.0f;
    float m_pixelsPerUnit = 100.0f;
    float m_grid = 0.5f;
    BrushDef m_def;
};

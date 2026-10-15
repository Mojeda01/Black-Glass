#include "BrushTool.h"
#include "BrushMesh.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <glm/gtc/matrix_transform.hpp>

namespace
{
    const glm::vec4 EDGE_COLOR(1.0f, 0.25f, 0.25f, 1.0f);
    const glm::vec4 EDGE_OVERLAY(1.0f, 0.25f, 0.25f, 0.3f);
    const glm::vec4 FOOTPRINT_COLOR(0.95f, 0.95f, 0.95f, 1.0f);
    const float LIFT = 0.003f;
    const float MAX_HEIGHT = 1000.0f;
    const float MIN_PIXELS_PER_UNIT = 30.0f;

    void formatSize(const glm::vec3& size, bool includeHeight, std::string& label)
    {
        char text[96];
        if (includeHeight)
        {
            std::snprintf(text, sizeof(text), "%.2f x %.2f x %.2f", size.x, size.y, size.z);
        }
        else
        {
            std::snprintf(text, sizeof(text), "%.2f x %.2f", size.x, size.z);
        }
        label = text;
    }
}

void BrushTool::arm()
{
    m_armed = true;
    m_phase = Phase::Hover;
    m_hasHover = false;
    m_confirm = false;
}

void BrushTool::disarm()
{
    m_armed = false;
    m_phase = Phase::Hover;
    m_hasHover = false;
    m_confirm = false;
}

bool BrushTool::cancel()
{
    if (!m_armed)
    {
        return false;
    }
    if (m_phase != Phase::Hover)
    {
        m_phase = Phase::Hover;
        m_confirm = false;
        return true;
    }
    disarm();
    return true;
}

bool BrushTool::onPress()
{
    if (!m_armed)
    {
        return false;
    }

    if (m_phase == Phase::Hover && m_hasHover)
    {
        m_phase = Phase::Footprint;
        m_start = m_hover;
        m_end = m_hover;
        m_baseY = m_hoverBase;
    }
    else if (m_phase == Phase::Height)
    {
        m_confirm = true;
    }
    return true;
}

void BrushTool::onRelease()
{
    if (!m_armed || m_phase != Phase::Footprint)
    {
        return;
    }

    if (m_end == m_start)
    {
        m_end = m_start + glm::vec2(m_grid);
    }
    m_height = m_grid;
    m_heightReady = false;
    m_phase = Phase::Height;
}

float BrushTool::directionYaw() const
{
    if (m_def.shape == BrushShape::Box)
    {
        return 0.0f;
    }

    glm::vec2 delta = m_end - m_start;
    if (std::abs(delta.x) < 1e-4f && std::abs(delta.y) < 1e-4f)
    {
        return 0.0f;
    }
    if (std::abs(delta.y) >= std::abs(delta.x))
    {
        return delta.y >= 0.0f ? 0.0f : 180.0f;
    }
    return delta.x >= 0.0f ? 90.0f : -90.0f;
}

BrushBounds BrushTool::currentBounds(float height) const
{
    glm::vec2 lo = glm::min(m_start, m_end);
    glm::vec2 hi = glm::max(m_start, m_end);
    if (hi.x - lo.x < m_grid)
    {
        hi.x = lo.x + m_grid;
    }
    if (hi.y - lo.y < m_grid)
    {
        hi.y = lo.y + m_grid;
    }

    float worldX = hi.x - lo.x;
    float worldZ = hi.y - lo.y;
    float yaw = directionYaw();
    bool sideways = std::abs(std::abs(yaw) - 90.0f) < 1.0f;

    BrushBounds bounds;
    bounds.center = glm::vec3((lo.x + hi.x) * 0.5f, m_baseY, (lo.y + hi.y) * 0.5f);
    float h = std::max(height, m_grid);
    bounds.size = sideways ? glm::vec3(worldZ, h, worldX) : glm::vec3(worldX, h, worldZ);
    bounds.yawDegrees = yaw;
    return bounds;
}

void BrushTool::drawPreview(DebugDraw& debug, float height) const
{
    BrushBounds bounds = currentBounds(height);

    glm::mat4 transform = glm::translate(glm::mat4(1.0f), bounds.center)
                        * glm::rotate(glm::mat4(1.0f), glm::radians(bounds.yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f))
                        * glm::scale(glm::mat4(1.0f), bounds.size);

    drawBrushOutline(m_def, transform, debug, EDGE_COLOR, false);
    drawBrushOutline(m_def, transform, debug, EDGE_OVERLAY, true);

    float yaw = glm::radians(bounds.yawDegrees);
    bool sideways = std::abs(std::abs(bounds.yawDegrees) - 90.0f) < 1.0f;
    float halfX = (sideways ? bounds.size.z : bounds.size.x) * 0.5f;
    float halfZ = (sideways ? bounds.size.x : bounds.size.z) * 0.5f;

    float y = m_baseY + LIFT;
    glm::vec3 a(bounds.center.x - halfX, y, bounds.center.z - halfZ);
    glm::vec3 b(bounds.center.x + halfX, y, bounds.center.z - halfZ);
    glm::vec3 c(bounds.center.x + halfX, y, bounds.center.z + halfZ);
    glm::vec3 d(bounds.center.x - halfX, y, bounds.center.z + halfZ);
    debug.line(a, b, FOOTPRINT_COLOR, true);
    debug.line(b, c, FOOTPRINT_COLOR, true);
    debug.line(c, d, FOOTPRINT_COLOR, true);
    debug.line(d, a, FOOTPRINT_COLOR, true);

    if (m_def.shape != BrushShape::Box)
    {
        glm::vec3 forward(std::sin(yaw), 0.0f, std::cos(yaw));
        glm::vec3 side(forward.z, 0.0f, -forward.x);
        float reach = bounds.size.z * 0.5f;
        glm::vec3 tail = glm::vec3(bounds.center.x, y, bounds.center.z) - forward * reach * 0.6f;
        glm::vec3 head = glm::vec3(bounds.center.x, y, bounds.center.z) + forward * reach * 0.6f;
        float wing = std::min(reach * 0.25f, 0.5f);
        debug.line(tail, head, FOOTPRINT_COLOR, true);
        debug.line(head, head - forward * wing + side * wing, FOOTPRINT_COLOR, true);
        debug.line(head, head - forward * wing - side * wing, FOOTPRINT_COLOR, true);
    }
}

bool BrushTool::update(const ViewInfo& view, bool hasRay, const glm::vec3& origin, const glm::vec3& direction,
                       bool cursorFree, const SurfaceHit& surface, const SnapSets& snaps, float grid,
                       bool visible, const BrushDef& def, DebugDraw& debug, BrushBounds& out, std::string& label)
{
    if (!m_armed)
    {
        return false;
    }

    m_grid = std::max(grid, 0.01f);
    m_def = def;

    switch (m_phase)
    {
        case Phase::Hover:
        {
            m_hasHover = surface.hit;
            if (surface.hit)
            {
                m_hover = glm::vec2(snapValue(surface.point.x, snaps.xs, m_grid),
                                    snapValue(surface.point.z, snaps.zs, m_grid));
                m_hoverBase = surface.baseY;
                if (visible)
                {
                    debug.cross(glm::vec3(m_hover.x, m_hoverBase + LIFT, m_hover.y), m_grid * 0.5f, FOOTPRINT_COLOR, true);
                }
            }
            label = surface.hit ? "Click and drag to draw a footprint" : "";
            break;
        }
        case Phase::Footprint:
        {
            if (hasRay && cursorFree && std::abs(direction.y) > 1e-5f)
            {
                float t = (m_baseY - origin.y) / direction.y;
                if (t > 0.0f && t < 500.0f)
                {
                    glm::vec3 hit = origin + direction * t;
                    m_end = glm::vec2(snapValue(hit.x, snaps.xs, m_grid), snapValue(hit.z, snaps.zs, m_grid));
                }
            }
            if (visible)
            {
                drawPreview(debug, m_grid);
            }
            formatSize(currentBounds(m_grid).size, false, label);
            break;
        }
        case Phase::Height:
        {
            if (!m_heightReady && view.valid)
            {
                glm::vec3 base(m_end.x, m_baseY, m_end.y);
                glm::vec2 p0;
                glm::vec2 p1;
                if (projectPoint(view, base, p0) && projectPoint(view, base + glm::vec3(0.0f, 1.0f, 0.0f), p1))
                {
                    m_pixelsPerUnit = std::max(p0.y - p1.y, MIN_PIXELS_PER_UNIT);
                }
                else
                {
                    m_pixelsPerUnit = 100.0f;
                }
                m_cursorStartY = view.cursor.y;
                m_heightStart = m_height;
                m_heightReady = true;
            }

            if (m_heightReady && cursorFree)
            {
                float raw = m_heightStart + (m_cursorStartY - view.cursor.y) / m_pixelsPerUnit;
                float top = snapValue(m_baseY + raw, snaps.ys, m_grid);
                m_height = std::clamp(top - m_baseY, m_grid, MAX_HEIGHT);
            }

            if (visible)
            {
                drawPreview(debug, m_height);
            }
            formatSize(currentBounds(m_height).size, true, label);
            break;
        }
    }

    if (m_confirm)
    {
        m_confirm = false;
        out = currentBounds(m_height);
        m_phase = Phase::Hover;
        m_hasHover = false;
        return true;
    }
    return false;
}

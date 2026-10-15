#include "BrushTool.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

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

    BrushBounds bounds;
    bounds.center = glm::vec3((lo.x + hi.x) * 0.5f, m_baseY, (lo.y + hi.y) * 0.5f);
    bounds.size = glm::vec3(hi.x - lo.x, std::max(height, m_grid), hi.y - lo.y);
    return bounds;
}

void BrushTool::drawPreview(DebugDraw& debug, float height) const
{
    BrushBounds bounds = currentBounds(height);
    glm::vec3 lo(bounds.center.x - bounds.size.x * 0.5f, m_baseY, bounds.center.z - bounds.size.z * 0.5f);
    glm::vec3 hi(bounds.center.x + bounds.size.x * 0.5f, m_baseY + bounds.size.y, bounds.center.z + bounds.size.z * 0.5f);

    debug.box(lo, hi, EDGE_COLOR, false);
    debug.box(lo, hi, EDGE_OVERLAY, true);

    float y = m_baseY + LIFT;
    glm::vec3 a(lo.x, y, lo.z);
    glm::vec3 b(hi.x, y, lo.z);
    glm::vec3 c(hi.x, y, hi.z);
    glm::vec3 d(lo.x, y, hi.z);
    debug.line(a, b, FOOTPRINT_COLOR, true);
    debug.line(b, c, FOOTPRINT_COLOR, true);
    debug.line(c, d, FOOTPRINT_COLOR, true);
    debug.line(d, a, FOOTPRINT_COLOR, true);
}

bool BrushTool::update(const ViewInfo& view, bool hasRay, const glm::vec3& origin, const glm::vec3& direction,
                       bool cursorFree, const SurfaceHit& surface, const SnapSets& snaps, float grid,
                       bool visible, DebugDraw& debug, BrushBounds& out, std::string& label)
{
    if (!m_armed)
    {
        return false;
    }

    m_grid = std::max(grid, 0.01f);

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

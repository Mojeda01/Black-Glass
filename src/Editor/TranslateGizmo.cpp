#include "TranslateGizmo.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
    const float HIT_RADIUS = 11.0f;
    const glm::vec4 COLOR_X(1.0f, 0.25f, 0.25f, 1.0f);
    const glm::vec4 COLOR_Y(0.3f, 1.0f, 0.35f, 1.0f);
    const glm::vec4 COLOR_Z(0.3f, 0.5f, 1.0f, 1.0f);
    const glm::vec4 COLOR_PLANE(1.0f, 0.85f, 0.2f, 1.0f);
    const glm::vec4 COLOR_HOVER(1.0f, 1.0f, 0.55f, 1.0f);
    const glm::vec4 COLOR_ACTIVE(1.0f, 1.0f, 1.0f, 1.0f);

    float segmentDistance(const glm::vec2& point, const glm::vec2& a, const glm::vec2& b)
    {
        glm::vec2 edge = b - a;
        float lengthSquared = glm::dot(edge, edge);
        if (lengthSquared < 1e-6f)
        {
            return glm::length(point - a);
        }
        float t = std::clamp(glm::dot(point - a, edge) / lengthSquared, 0.0f, 1.0f);
        return glm::length(point - (a + edge * t));
    }

    void arrow(DebugDraw& debug, const glm::vec3& from, const glm::vec3& axis, const glm::vec3& sideA,
               const glm::vec3& sideB, float length, const glm::vec4& color)
    {
        glm::vec3 tip = from + axis * length;
        debug.line(from, tip, color, true);
        float head = length * 0.14f;
        float width = head * 0.45f;
        glm::vec3 base = tip - axis * head;
        debug.line(tip, base + sideA * width, color, true);
        debug.line(tip, base - sideA * width, color, true);
        debug.line(tip, base + sideB * width, color, true);
        debug.line(tip, base - sideB * width, color, true);
        debug.line(base + sideA * width, base + sideB * width, color, true);
        debug.line(base + sideB * width, base - sideA * width, color, true);
        debug.line(base - sideA * width, base - sideB * width, color, true);
        debug.line(base - sideB * width, base + sideA * width, color, true);
    }
}

bool TranslateGizmo::dragPoint(Handle handle, const glm::vec3& origin, const glm::vec3& direction,
                               glm::vec3& out) const
{
    if (handle == Handle::PlaneXZ)
    {
        if (std::abs(direction.y) < 1e-5f)
        {
            return false;
        }
        float t = (m_anchor.y - origin.y) / direction.y;
        if (t <= 0.0f)
        {
            return false;
        }
        out = origin + direction * t;
        return true;
    }

    glm::vec3 axis(0.0f);
    if (handle == Handle::AxisX)
    {
        axis.x = 1.0f;
    }
    else if (handle == Handle::AxisY)
    {
        axis.y = 1.0f;
    }
    else
    {
        axis.z = 1.0f;
    }

    glm::vec3 offset = m_anchor - origin;
    float b = glm::dot(axis, direction);
    float d = glm::dot(axis, offset);
    float e = glm::dot(direction, offset);
    float denominator = 1.0f - b * b;
    if (denominator < 1e-5f)
    {
        return false;
    }
    float t = (b * e - d) / denominator;
    out = m_anchor + axis * t;
    return true;
}

bool TranslateGizmo::onPress()
{
    if (m_dragging || m_handle == Handle::None || !m_target.valid)
    {
        return false;
    }
    m_dragging = true;
    m_started = false;
    m_anchor = m_target.center;
    m_applied = glm::vec3(0.0f);
    return true;
}

void TranslateGizmo::onRelease()
{
    m_dragging = false;
    m_started = false;
}

bool TranslateGizmo::update(const ViewInfo& view, const TranslateTarget& target, bool hasRay,
                            const glm::vec3& origin, const glm::vec3& direction, bool cursorFree,
                            float snapStep, bool visible, DebugDraw& debug, glm::vec3& step,
                            std::string& label)
{
    step = glm::vec3(0.0f);
    m_target = target;

    if (!target.valid || !view.valid)
    {
        m_handle = Handle::None;
        m_dragging = false;
        m_started = false;
        return false;
    }

    glm::vec3 center = target.center;
    float distanceToCamera = glm::length(view.cameraPos - center);
    float length = std::max(distanceToCamera * 0.16f, 0.6f);
    float plane = length * 0.3f;

    if (!m_dragging)
    {
        m_handle = Handle::None;
        if (cursorFree)
        {
            glm::vec2 screenCenter;
            glm::vec2 screenX;
            glm::vec2 screenY;
            glm::vec2 screenZ;
            glm::vec2 screenPlane;
            bool ok = projectPoint(view, center, screenCenter) &&
                      projectPoint(view, center + glm::vec3(length, 0.0f, 0.0f), screenX) &&
                      projectPoint(view, center + glm::vec3(0.0f, length, 0.0f), screenY) &&
                      projectPoint(view, center + glm::vec3(0.0f, 0.0f, length), screenZ) &&
                      projectPoint(view, center + glm::vec3(plane * 0.6f, 0.0f, plane * 0.6f), screenPlane);
            if (ok)
            {
                float best = HIT_RADIUS;
                float distance = glm::length(view.cursor - screenPlane);
                if (distance < best)
                {
                    best = distance;
                    m_handle = Handle::PlaneXZ;
                }
                distance = segmentDistance(view.cursor, screenCenter, screenX);
                if (distance < best)
                {
                    best = distance;
                    m_handle = Handle::AxisX;
                }
                distance = segmentDistance(view.cursor, screenCenter, screenY);
                if (distance < best)
                {
                    best = distance;
                    m_handle = Handle::AxisY;
                }
                distance = segmentDistance(view.cursor, screenCenter, screenZ);
                if (distance < best)
                {
                    best = distance;
                    m_handle = Handle::AxisZ;
                }
            }
        }
    }

    bool changed = false;
    if (m_dragging && hasRay)
    {
        glm::vec3 point;
        if (dragPoint(m_handle, origin, direction, point))
        {
            if (!m_started)
            {
                m_started = true;
                m_startPoint = point;
            }

            glm::vec3 total = point - m_startPoint;
            if (m_handle == Handle::AxisX)
            {
                total.y = 0.0f;
                total.z = 0.0f;
            }
            else if (m_handle == Handle::AxisY)
            {
                total.x = 0.0f;
                total.z = 0.0f;
            }
            else if (m_handle == Handle::AxisZ)
            {
                total.x = 0.0f;
                total.y = 0.0f;
            }
            else
            {
                total.y = 0.0f;
            }

            if (snapStep > 0.0f)
            {
                total = glm::vec3(std::round(total.x / snapStep), std::round(total.y / snapStep),
                                  std::round(total.z / snapStep)) * snapStep;
            }

            step = total - m_applied;
            m_applied = total;
            changed = glm::dot(step, step) > 1e-10f;

            char text[64];
            std::snprintf(text, sizeof(text), "Move %+.2f  %+.2f  %+.2f", m_applied.x, m_applied.y, m_applied.z);
            label = text;
        }
    }

    if (visible)
    {
        auto tint = [&](Handle handle, const glm::vec4& base) -> const glm::vec4&
        {
            if (m_handle == handle)
            {
                return m_dragging ? COLOR_ACTIVE : COLOR_HOVER;
            }
            return base;
        };

        arrow(debug, center, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f),
              length, tint(Handle::AxisX, COLOR_X));
        arrow(debug, center, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f),
              length, tint(Handle::AxisY, COLOR_Y));
        arrow(debug, center, glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f),
              length, tint(Handle::AxisZ, COLOR_Z));

        const glm::vec4& planeColor = tint(Handle::PlaneXZ, COLOR_PLANE);
        glm::vec3 a = center + glm::vec3(plane * 0.2f, 0.0f, plane * 0.2f);
        glm::vec3 b = center + glm::vec3(plane, 0.0f, plane * 0.2f);
        glm::vec3 c = center + glm::vec3(plane, 0.0f, plane);
        glm::vec3 d = center + glm::vec3(plane * 0.2f, 0.0f, plane);
        debug.line(a, b, planeColor, true);
        debug.line(b, c, planeColor, true);
        debug.line(c, d, planeColor, true);
        debug.line(d, a, planeColor, true);
    }

    return changed;
}

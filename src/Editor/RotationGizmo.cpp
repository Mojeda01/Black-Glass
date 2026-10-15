#include "RotationGizmo.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
    const float PI = 3.14159265358979f;
    const int SEGMENTS = 96;
    const glm::vec4 RING_COLOR(0.25f, 0.8f, 1.0f, 0.9f);
    const glm::vec4 RING_HOVER(1.0f, 0.9f, 0.2f, 1.0f);
    const glm::vec4 RING_ACTIVE(1.0f, 0.3f, 0.2f, 1.0f);

    bool planeHit(const glm::vec3& origin, const glm::vec3& direction, float planeY, glm::vec3& hit)
    {
        if (std::abs(direction.y) < 1e-5f)
        {
            return false;
        }
        float t = (planeY - origin.y) / direction.y;
        if (t <= 0.0f)
        {
            return false;
        }
        hit = origin + direction * t;
        return true;
    }

    float wrapAngle(float angle)
    {
        while (angle > PI)
        {
            angle -= 2.0f * PI;
        }
        while (angle < -PI)
        {
            angle += 2.0f * PI;
        }
        return angle;
    }
}

bool RotationGizmo::onPress()
{
    if (m_dragging || !m_hover || !m_target.valid)
    {
        return false;
    }
    m_dragging = true;
    m_accumulated = 0.0f;
    m_applied = 0.0f;
    m_lastAngle = std::numeric_limits<float>::quiet_NaN();
    return true;
}

void RotationGizmo::onRelease()
{
    m_dragging = false;
}

bool RotationGizmo::update(const ViewInfo& view, const RotationTarget& target, bool hasRay,
                           const glm::vec3& origin, const glm::vec3& direction, bool cursorFree,
                           float snapDegrees, bool visible, DebugDraw& debug, float& stepDegrees,
                           std::string& label)
{
    stepDegrees = 0.0f;
    m_target = target;

    if (!target.valid || !view.valid)
    {
        m_hover = false;
        m_dragging = false;
        return false;
    }

    float ringY = target.center.y + 0.01f;
    float distanceToCamera = glm::length(view.cameraPos - target.center);
    float tolerance = std::max(0.12f, distanceToCamera * 0.02f);

    glm::vec3 hit(0.0f);
    bool hasHit = hasRay && planeHit(origin, direction, ringY, hit);
    float angle = 0.0f;
    if (hasHit)
    {
        angle = std::atan2(hit.z - target.center.z, hit.x - target.center.x);
    }

    if (!m_dragging)
    {
        m_hover = false;
        if (cursorFree && hasHit)
        {
            float distance = glm::length(glm::vec2(hit.x - target.center.x, hit.z - target.center.z));
            m_hover = std::abs(distance - target.radius) < tolerance;
        }
    }

    bool changed = false;
    if (m_dragging && hasHit)
    {
        if (std::isnan(m_lastAngle))
        {
            m_lastAngle = angle;
            m_startAngle = angle;
        }
        m_accumulated += wrapAngle(angle - m_lastAngle);
        m_lastAngle = angle;

        float yaw = -m_accumulated * 180.0f / PI;
        float snapped = snapDegrees > 0.0f ? std::round(yaw / snapDegrees) * snapDegrees : yaw;
        stepDegrees = snapped - m_applied;
        m_applied = snapped;
        changed = std::abs(stepDegrees) > 1e-4f;

        char text[48];
        std::snprintf(text, sizeof(text), "Yaw %+.1f deg", m_applied);
        label = text;
    }

    if (visible)
    {
        const glm::vec4& color = m_dragging ? RING_ACTIVE : (m_hover ? RING_HOVER : RING_COLOR);
        glm::vec3 previous;
        for (int i = 0; i <= SEGMENTS; i++)
        {
            float a = 2.0f * PI * static_cast<float>(i) / static_cast<float>(SEGMENTS);
            glm::vec3 point(target.center.x + std::cos(a) * target.radius, ringY,
                            target.center.z + std::sin(a) * target.radius);
            if (i > 0)
            {
                debug.line(previous, point, color, true);
            }
            previous = point;
        }

        if (m_dragging)
        {
            float current = m_startAngle - m_applied * PI / 180.0f;
            glm::vec3 centre(target.center.x, ringY, target.center.z);
            glm::vec3 tip(target.center.x + std::cos(current) * target.radius, ringY,
                          target.center.z + std::sin(current) * target.radius);
            debug.line(centre, tip, color, true);
            debug.line(centre, centre + glm::vec3(std::cos(m_startAngle), 0.0f, std::sin(m_startAngle)) * target.radius,
                       glm::vec4(1.0f, 1.0f, 1.0f, 0.35f), true);
        }
    }

    return changed;
}

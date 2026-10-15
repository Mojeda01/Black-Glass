#include "BrushHandles.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
    const int FACE_COUNT = 6;
    const int FACE_AXIS[FACE_COUNT] = { 0, 0, 2, 2, 1, 1 };
    const float FACE_SIGN[FACE_COUNT] = { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f };

    const float HOVER_RADIUS = 12.0f;
    const float HANDLE_SCALE = 0.008f;

    const glm::vec4 HANDLE_COLOR(1.0f, 0.55f, 0.1f, 1.0f);
    const glm::vec4 HANDLE_ACTIVE(1.0f, 0.2f, 0.2f, 1.0f);

    void faceFrame(const HandleTarget& target, int face, glm::vec3& normal, glm::vec3& center)
    {
        glm::mat3 rotation(glm::rotate(glm::mat4(1.0f), glm::radians(target.yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f)));
        glm::vec3 axes[3] = { rotation * glm::vec3(1.0f, 0.0f, 0.0f),
                              glm::vec3(0.0f, 1.0f, 0.0f),
                              rotation * glm::vec3(0.0f, 0.0f, 1.0f) };

        int axis = FACE_AXIS[face];
        normal = axes[axis] * FACE_SIGN[face];
        glm::vec3 boxCenter = target.position + glm::vec3(0.0f, target.size.y * 0.5f, 0.0f);
        center = boxCenter + normal * (target.size[axis] * 0.5f);
    }

    void formatSize(const glm::vec3& size, std::string& label)
    {
        char text[96];
        std::snprintf(text, sizeof(text), "%.2f x %.2f x %.2f", size.x, size.y, size.z);
        label = text;
    }
}

bool BrushHandles::onPress()
{
    if (m_dragging || m_hover < 0 || !m_target.valid || !m_view.valid)
    {
        return false;
    }

    glm::vec3 normal;
    glm::vec3 center;
    faceFrame(m_target, m_hover, normal, center);

    glm::vec2 p0;
    glm::vec2 p1;
    if (!projectPoint(m_view, center, p0) || !projectPoint(m_view, center + normal, p1))
    {
        return true;
    }

    glm::vec2 direction = p1 - p0;
    if (glm::dot(direction, direction) < 1e-4f)
    {
        return true;
    }

    m_dragging = true;
    m_face = m_hover;
    m_normal = normal;
    m_faceStart = center;
    m_sizeStart = m_target.size;
    m_positionStart = m_target.position;
    m_mouseStart = m_view.cursor;
    m_screenDirection = direction;
    return true;
}

void BrushHandles::onRelease()
{
    m_dragging = false;
}

bool BrushHandles::update(const ViewInfo& view, const HandleTarget& target, bool cursorFree, float grid,
                          const SnapSets& snaps, bool visible, DebugDraw& debug, HandleEdit& edit, std::string& label)
{
    m_view = view;
    m_target = target;

    if (!target.valid || !view.valid)
    {
        m_hover = -1;
        m_dragging = false;
        return false;
    }

    if (!m_dragging)
    {
        m_hover = -1;
        if (cursorFree)
        {
            float best = HOVER_RADIUS;
            for (int i = 0; i < FACE_COUNT; i++)
            {
                glm::vec3 normal;
                glm::vec3 center;
                faceFrame(target, i, normal, center);

                glm::vec2 screen;
                if (!projectPoint(view, center, screen))
                {
                    continue;
                }
                float distance = glm::length(screen - view.cursor);
                if (distance < best)
                {
                    best = distance;
                    m_hover = i;
                }
            }
        }
    }

    bool changed = false;
    if (m_dragging)
    {
        float raw = glm::dot(view.cursor - m_mouseStart, m_screenDirection)
                  / glm::dot(m_screenDirection, m_screenDirection);

        int dimension = FACE_AXIS[m_face];
        float move = 0.0f;

        int worldAxis = -1;
        const std::vector<float>* candidates = nullptr;
        if (std::abs(m_normal.x) > 0.999f)
        {
            worldAxis = 0;
            candidates = &snaps.xs;
        }
        else if (std::abs(m_normal.y) > 0.999f)
        {
            worldAxis = 1;
            candidates = &snaps.ys;
        }
        else if (std::abs(m_normal.z) > 0.999f)
        {
            worldAxis = 2;
            candidates = &snaps.zs;
        }

        if (worldAxis >= 0)
        {
            float start = m_faceStart[worldAxis];
            float value = start + raw * m_normal[worldAxis];
            float snapped = snapValue(value, *candidates, grid);
            move = (snapped - start) / m_normal[worldAxis];
        }
        else
        {
            move = std::round(raw / grid) * grid;
        }

        move = std::max(move, -(m_sizeStart[dimension] - grid));

        edit.size = m_sizeStart;
        edit.size[dimension] += move;
        edit.position = m_positionStart;
        if (dimension != 1)
        {
            edit.position += m_normal * (move * 0.5f);
        }
        else if (FACE_SIGN[m_face] < 0.0f)
        {
            edit.position.y = m_positionStart.y - move;
        }

        formatSize(edit.size, label);
        changed = true;
    }

    if (visible)
    {
        for (int i = 0; i < FACE_COUNT; i++)
        {
            glm::vec3 normal;
            glm::vec3 center;
            faceFrame(target, i, normal, center);

            bool active = (m_dragging && i == m_face) || (!m_dragging && i == m_hover);
            float half = std::max(glm::length(view.cameraPos - center) * HANDLE_SCALE, 0.02f);
            if (active)
            {
                half *= 1.4f;
            }
            debug.box(center - glm::vec3(half), center + glm::vec3(half),
                      active ? HANDLE_ACTIVE : HANDLE_COLOR, true);
        }
    }

    return changed;
}

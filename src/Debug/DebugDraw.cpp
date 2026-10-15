#include "DebugDraw.h"

#include <algorithm>
#include <cmath>

void DebugDraw::clear()
{
    m_tested.clear();
    m_overlay.clear();
}

void DebugDraw::line(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color, bool overlay)
{
    line(a, b, color, color, overlay);
}

void DebugDraw::line(const glm::vec3& a, const glm::vec3& b, const glm::vec4& colorA, const glm::vec4& colorB, bool overlay)
{
    std::vector<DebugVertex>& target = overlay ? m_overlay : m_tested;
    target.push_back({ a, colorA });
    target.push_back({ b, colorB });
}

void DebugDraw::box(const glm::vec3& lo, const glm::vec3& hi, const glm::vec4& color, bool overlay)
{
    obb(glm::mat4(1.0f), lo, hi, color, overlay);
}

void DebugDraw::obb(const glm::mat4& transform, const glm::vec3& lo, const glm::vec3& hi, const glm::vec4& color, bool overlay)
{
    glm::vec3 corners[8];
    for (int i = 0; i < 8; i++)
    {
        glm::vec3 local((i & 1) ? hi.x : lo.x,
                        (i & 2) ? hi.y : lo.y,
                        (i & 4) ? hi.z : lo.z);
        corners[i] = glm::vec3(transform * glm::vec4(local, 1.0f));
    }

    const int edges[12][2] = {
        { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 },
        { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 },
        { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
    };
    for (const auto& edge : edges)
    {
        line(corners[edge[0]], corners[edge[1]], color, overlay);
    }
}

void DebugDraw::cross(const glm::vec3& point, float size, const glm::vec4& color, bool overlay)
{
    line(point - glm::vec3(size, 0.0f, 0.0f), point + glm::vec3(size, 0.0f, 0.0f), color, overlay);
    line(point - glm::vec3(0.0f, size, 0.0f), point + glm::vec3(0.0f, size, 0.0f), color, overlay);
    line(point - glm::vec3(0.0f, 0.0f, size), point + glm::vec3(0.0f, 0.0f, size), color, overlay);
}

void DebugDraw::grid(const glm::vec3& center, int halfCells, float spacing, float y,
                     const glm::vec4& minor, const glm::vec4& major, int majorEvery)
{
    if (halfCells <= 0 || spacing <= 0.0f)
    {
        return;
    }

    const float radius = halfCells * spacing;
    const int segments = 16;
    const int every = std::max(majorEvery, 1);

    auto fade = [&](const glm::vec3& p)
    {
        float r = glm::length(glm::vec2(p.x - center.x, p.z - center.z)) / radius;
        float f = std::clamp(1.0f - r, 0.0f, 1.0f);
        return f * f;
    };

    for (int axis = 0; axis < 2; axis++)
    {
        for (int i = -halfCells; i <= halfCells; i++)
        {
            float fixedCoord = (axis == 0 ? center.x : center.z) + i * spacing;
            long worldIndex = std::lround(fixedCoord / spacing);
            bool isMajor = ((worldIndex % every) + every) % every == 0;
            glm::vec4 base = isMajor ? major : minor;

            for (int k = 0; k < segments; k++)
            {
                float t0 = -1.0f + 2.0f * k / segments;
                float t1 = -1.0f + 2.0f * (k + 1) / segments;
                float along0 = (axis == 0 ? center.z : center.x) + t0 * radius;
                float along1 = (axis == 0 ? center.z : center.x) + t1 * radius;

                glm::vec3 p0 = axis == 0 ? glm::vec3(fixedCoord, y, along0) : glm::vec3(along0, y, fixedCoord);
                glm::vec3 p1 = axis == 0 ? glm::vec3(fixedCoord, y, along1) : glm::vec3(along1, y, fixedCoord);

                glm::vec4 c0 = base;
                glm::vec4 c1 = base;
                c0.a *= fade(p0);
                c1.a *= fade(p1);
                if (c0.a < 0.01f && c1.a < 0.01f)
                {
                    continue;
                }
                line(p0, p1, c0, c1, false);
            }
        }
    }
}

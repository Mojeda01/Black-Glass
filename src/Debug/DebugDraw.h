#pragma once
#include <glm/glm.hpp>

#include <vector>

struct DebugVertex
{
    glm::vec3 pos;
    glm::vec4 color;
};

class DebugDraw
{
public:
    void clear();

    void line(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color, bool overlay = false);
    void line(const glm::vec3& a, const glm::vec3& b, const glm::vec4& colorA, const glm::vec4& colorB, bool overlay = false);
    void box(const glm::vec3& lo, const glm::vec3& hi, const glm::vec4& color, bool overlay = false);
    void obb(const glm::mat4& transform, const glm::vec3& lo, const glm::vec3& hi, const glm::vec4& color, bool overlay = false);
    void cross(const glm::vec3& point, float size, const glm::vec4& color, bool overlay = false);
    void grid(const glm::vec3& center, int halfCells, float spacing, float y,
              const glm::vec4& minor, const glm::vec4& major, int majorEvery);

    const std::vector<DebugVertex>& tested() const { return m_tested; }
    const std::vector<DebugVertex>& overlay() const { return m_overlay; }

private:
    std::vector<DebugVertex> m_tested;
    std::vector<DebugVertex> m_overlay;
};

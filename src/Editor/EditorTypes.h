#pragma once
#include <glm/glm.hpp>

#include <cmath>
#include <string>
#include <vector>

enum class ItemKind
{
    Box,
    Ramp,
    Stairs,
    Wedge,
    Cylinder,
    Model,
    Light,
    Spawn
};

struct ItemInfo
{
    int id = -1;
    std::string name;
    ItemKind kind = ItemKind::Box;
    bool visible = true;
    bool locked = false;
    bool solid = true;
    bool selected = false;
    glm::vec3 position{ 0.0f };
    float yawDegrees = 0.0f;
    float scale = 1.0f;
    glm::vec3 nativeSize{ 0.0f };
    int group = 0;
};

struct ViewInfo
{
    glm::mat4 viewProj{ 1.0f };
    glm::vec2 windowSize{ 0.0f };
    glm::vec2 cursor{ 0.0f };
    glm::vec3 cameraPos{ 0.0f };
    bool valid = false;
};

struct SurfaceHit
{
    bool hit = false;
    glm::vec3 point{ 0.0f };
    glm::vec3 normal{ 0.0f, 1.0f, 0.0f };
    float baseY = 0.0f;
};

struct SnapSets
{
    std::vector<float> xs;
    std::vector<float> ys;
    std::vector<float> zs;
};

inline float snapValue(float value, const std::vector<float>& candidates, float grid)
{
    float best = value;
    float bestDistance = grid * 0.35f;
    bool found = false;
    for (float candidate : candidates)
    {
        float distance = std::abs(candidate - value);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = candidate;
            found = true;
        }
    }
    return found ? best : std::round(value / grid) * grid;
}

inline bool projectPoint(const ViewInfo& view, const glm::vec3& world, glm::vec2& out)
{
    if (!view.valid)
    {
        return false;
    }
    glm::vec4 clip = view.viewProj * glm::vec4(world, 1.0f);
    if (clip.w <= 1e-4f)
    {
        return false;
    }
    glm::vec2 ndc(clip.x / clip.w, clip.y / clip.w);
    out = glm::vec2((ndc.x * 0.5f + 0.5f) * view.windowSize.x,
                    (ndc.y * 0.5f + 0.5f) * view.windowSize.y);
    return true;
}

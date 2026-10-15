#pragma once
#include <glm/glm.hpp>

enum class BrushShape
{
    Box,
    Ramp,
    Stairs
};

constexpr int BRUSH_SHAPE_COUNT = 3;
constexpr const char* BRUSH_SHAPE_NAMES[BRUSH_SHAPE_COUNT] = { "Box", "Ramp", "Stairs" };

struct BrushDef
{
    BrushShape shape = BrushShape::Box;
    glm::vec3 size{ 2.0f, 2.0f, 2.0f };
    int steps = 8;
    glm::vec3 color{ 0.5f, 0.5f, 0.5f };
    float roughness = 0.6f;
    float metallic = 0.0f;

    bool operator==(const BrushDef&) const = default;
};

#pragma once
#include <glm/glm.hpp>

enum class BrushShape
{
    Box
};

struct BrushDef
{
    BrushShape shape = BrushShape::Box;
    glm::vec3 size{ 2.0f, 2.0f, 2.0f };
    glm::vec3 color{ 0.5f, 0.5f, 0.5f };
    float roughness = 0.6f;
    float metallic = 0.0f;
};

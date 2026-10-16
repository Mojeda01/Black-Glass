#pragma once
#include <glm/glm.hpp>

struct LightDef
{
    glm::vec3 color{ 1.0f, 0.85f, 0.6f };
    float intensity = 15.0f;
    float radius = 10.0f;
    bool enabled = true;
    bool spot = false;
    float pitch = -60.0f;
    float innerAngle = 20.0f;
    float outerAngle = 35.0f;

    bool operator==(const LightDef&) const = default;
};

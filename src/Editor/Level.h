#pragma once
#include "Brush.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

struct LevelBrush
{
    std::string name;
    BrushDef def;
    glm::vec3 position{ 0.0f };
    float yawDegrees = 0.0f;
};

struct LevelModel
{
    std::string name;
    std::string asset;
    glm::vec3 position{ 0.0f };
    float yawDegrees = 0.0f;
    float scale = 1.0f;
};

struct LevelData
{
    std::vector<LevelBrush> brushes;
    std::vector<LevelModel> models;
};

#pragma once
#include "Brush.h"
#include "../RenderSettings.h"

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

struct LevelCamera
{
    glm::vec3 position{ 0.0f, 2.0f, 6.0f };
    float yaw = -90.0f;
    float pitch = 0.0f;
};

struct LevelData
{
    std::vector<LevelBrush> brushes;
    std::vector<LevelModel> models;
    bool hasSettings = false;
    RenderSettings settings;
    bool hasCamera = false;
    LevelCamera camera;
};

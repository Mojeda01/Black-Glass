#pragma once
#include "Brush.h"
#include "Light.h"
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
    bool selected = false;
    bool hidden = false;
    bool locked = false;
};

struct LevelModel
{
    std::string name;
    std::string asset;
    glm::vec3 position{ 0.0f };
    float yawDegrees = 0.0f;
    float scale = 1.0f;
    bool selected = false;
    bool hidden = false;
    bool locked = false;
};

struct LevelLight
{
    std::string name;
    LightDef def;
    glm::vec3 position{ 0.0f };
    bool selected = false;
    bool hidden = false;
    bool locked = false;
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
    std::vector<LevelLight> lights;
    bool hasSettings = false;
    RenderSettings settings;
    bool hasCamera = false;
    LevelCamera camera;
};

inline bool sameContent(const LevelData& a, const LevelData& b)
{
    if (a.brushes.size() != b.brushes.size() || a.models.size() != b.models.size() ||
        a.lights.size() != b.lights.size())
    {
        return false;
    }
    if (a.hasSettings != b.hasSettings || (a.hasSettings && !(a.settings == b.settings)))
    {
        return false;
    }
    for (size_t i = 0; i < a.brushes.size(); i++)
    {
        const LevelBrush& x = a.brushes[i];
        const LevelBrush& y = b.brushes[i];
        if (x.name != y.name || !(x.def == y.def) || x.position != y.position || x.yawDegrees != y.yawDegrees ||
            x.hidden != y.hidden || x.locked != y.locked)
        {
            return false;
        }
    }
    for (size_t i = 0; i < a.lights.size(); i++)
    {
        const LevelLight& x = a.lights[i];
        const LevelLight& y = b.lights[i];
        if (x.name != y.name || !(x.def == y.def) || x.position != y.position ||
            x.hidden != y.hidden || x.locked != y.locked)
        {
            return false;
        }
    }
    for (size_t i = 0; i < a.models.size(); i++)
    {
        const LevelModel& x = a.models[i];
        const LevelModel& y = b.models[i];
        if (x.name != y.name || x.asset != y.asset || x.position != y.position ||
            x.yawDegrees != y.yawDegrees || x.scale != y.scale ||
            x.hidden != y.hidden || x.locked != y.locked)
        {
            return false;
        }
    }
    return true;
}

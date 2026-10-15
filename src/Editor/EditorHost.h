#pragma once
#include "Brush.h"
#include "Level.h"

#include <string>

class EditorHost
{
public:
    virtual ~EditorHost() = default;

    virtual std::string assetRoot() const = 0;
    virtual glm::vec3 spawnPoint() const = 0;

    virtual LevelData captureLevel() const = 0;
    virtual void applyEnvironment(const LevelData& level) = 0;
    virtual void clearLevel() = 0;
    virtual bool spawnBrush(const LevelBrush& brush, bool select) = 0;
    virtual bool spawnModel(const LevelModel& model) = 0;

    virtual BrushDef* selectedBrush() = 0;
    virtual void applySelectedBrush() = 0;
    virtual bool duplicateSelected(const glm::vec3& offset) = 0;
    virtual void snapSelectedToGrid(float grid) = 0;
};

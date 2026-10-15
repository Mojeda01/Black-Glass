#pragma once
#include "Brush.h"
#include "Level.h"
#include "EditorTypes.h"
#include "../Debug/DebugDraw.h"

#include <string>
#include <vector>

class EditorHost
{
public:
    virtual ~EditorHost() = default;

    virtual std::string assetRoot() const = 0;
    virtual DebugDraw& debug() = 0;
    virtual bool cursorRay(glm::vec3& origin, glm::vec3& direction) const = 0;
    virtual bool cursorFree() const = 0;
    virtual int selectionCount() const = 0;
    virtual bool selectionBounds(int index, glm::mat4& transform, glm::vec3& localMin, glm::vec3& localMax) const = 0;
    virtual void clearSelection() = 0;
    virtual void selectAll() = 0;
    virtual void cancelPlacement() = 0;
    virtual bool viewInfo(ViewInfo& view) const = 0;
    virtual bool pickSurface(const glm::vec3& origin, const glm::vec3& direction, SurfaceHit& hit) const = 0;
    virtual void collectSnaps(SnapSets& snaps, bool excludeSelected) const = 0;
    virtual bool selectedBrushFrame(glm::vec3& position, float& yawDegrees, glm::vec3& size) = 0;
    virtual void setSelectedBrush(const glm::vec3& position, const glm::vec3& size) = 0;
    virtual glm::vec3 spawnPoint() const = 0;

    virtual LevelData captureLevel() const = 0;
    virtual void applyEnvironment(const LevelData& level) = 0;
    virtual void clearLevel() = 0;
    virtual bool spawnBrush(const LevelBrush& brush, bool select) = 0;
    virtual bool spawnModel(const LevelModel& model) = 0;
    virtual bool spawnLight(const LevelLight& light, bool select) = 0;
    virtual LightDef* selectedLight() = 0;
    virtual void setLightMarkersVisible(bool visible) = 0;
    virtual std::vector<std::string> textureNames() = 0;

    virtual BrushDef* selectedBrush() = 0;
    virtual void applySelectedBrush() = 0;
    virtual bool duplicateSelected(const glm::vec3& offset) = 0;
    virtual void snapSelectedToGrid(float grid) = 0;
    virtual void rotateSelected(float degrees) = 0;
};

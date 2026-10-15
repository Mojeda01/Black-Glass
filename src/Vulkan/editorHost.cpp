#include "../BlackGlass.h"
#include "../Editor/BrushMesh.h"
#include "../Editor/Editor.h"
#include "../Editor/EditorHost.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <filesystem>
#include <iostream>

namespace
{
    bool sameGeometry(const BrushDef& a, const BrushDef& b)
    {
        if (a.shape != b.shape)
        {
            return false;
        }
        return a.shape != BrushShape::Stairs || a.steps == b.steps;
    }

    bool sameMaterial(const BrushDef& a, const BrushDef& b)
    {
        return a.color == b.color && a.roughness == b.roughness && a.metallic == b.metallic;
    }

    bool sameAsset(const BrushDef& a, const BrushDef& b)
    {
        return sameGeometry(a, b) && sameMaterial(a, b);
    }

    bool rayTriangle(const glm::vec3& origin, const glm::vec3& direction,
                     const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, float& t)
    {
        glm::vec3 edge1 = b - a;
        glm::vec3 edge2 = c - a;
        glm::vec3 p = glm::cross(direction, edge2);
        float determinant = glm::dot(edge1, p);
        if (std::abs(determinant) < 1e-9f)
        {
            return false;
        }
        float inverse = 1.0f / determinant;
        glm::vec3 s = origin - a;
        float u = glm::dot(s, p) * inverse;
        if (u < 0.0f || u > 1.0f)
        {
            return false;
        }
        glm::vec3 q = glm::cross(s, edge1);
        float v = glm::dot(direction, q) * inverse;
        if (v < 0.0f || u + v > 1.0f)
        {
            return false;
        }
        t = glm::dot(edge2, q) * inverse;
        return t > 1e-6f;
    }
}

class BlackGlassEditorHost : public EditorHost
{
public:
    explicit BlackGlassEditorHost(BlackGlass& engine)
        : m_engine(engine)
    {
    }

    std::string assetRoot() const override
    {
        return std::string(BLACK_GLASS_ASSET_DIR);
    }

    DebugDraw& debug() override
    {
        return m_engine.m_debugDraw;
    }

    bool cursorRay(glm::vec3& origin, glm::vec3& direction) const override
    {
        return m_engine.cursorRay(origin, direction);
    }

    bool cursorFree() const override
    {
        return !m_engine.m_mouseCaptured && !m_engine.uiWantsMouse();
    }

    bool selectedBounds(glm::mat4& transform, glm::vec3& localMin, glm::vec3& localMax) const override
    {
        const Model* model = m_engine.m_selected;
        if (!model || model->ghost || !model->visible || !model->asset)
        {
            return false;
        }
        transform = model->transform;
        localMin = model->asset->localMin;
        localMax = model->asset->localMax;
        return true;
    }

    void cancelPlacement() override
    {
        m_engine.cancelPlacement();
    }

    bool viewInfo(ViewInfo& view) const override
    {
        int width = 0;
        int height = 0;
        glfwGetWindowSize(m_engine.m_window, &width, &height);
        if (width <= 0 || height <= 0)
        {
            view.valid = false;
            return false;
        }

        double cursorX = 0.0;
        double cursorY = 0.0;
        glfwGetCursorPos(m_engine.m_window, &cursorX, &cursorY);

        glm::mat4 projection = glm::perspective(
            glm::radians(m_engine.m_fov),
            m_engine.M_SWAPCHAINEXTENT.width / static_cast<float>(std::max(m_engine.M_SWAPCHAINEXTENT.height, 1u)),
            0.1f, 1000.0f);
        projection[1][1] *= -1;

        view.viewProj = projection * m_engine.m_camera.view();
        view.windowSize = glm::vec2(static_cast<float>(width), static_cast<float>(height));
        view.cursor = glm::vec2(static_cast<float>(cursorX), static_cast<float>(cursorY));
        view.cameraPos = m_engine.m_camera.position;
        view.valid = true;
        return true;
    }

    bool pickSurface(const glm::vec3& origin, const glm::vec3& direction, SurfaceHit& hit) const override
    {
        float bestT = std::numeric_limits<float>::max();
        hit = SurfaceHit{};

        if (direction.y < -1e-5f)
        {
            float t = -origin.y / direction.y;
            if (t > 0.0f && t < 500.0f)
            {
                bestT = t;
                hit.hit = true;
                hit.point = origin + direction * t;
                hit.normal = glm::vec3(0.0f, 1.0f, 0.0f);
                hit.baseY = 0.0f;
            }
        }

        for (const auto& model : m_engine.M_MODELS)
        {
            if (!model->brush || model->ghost || !model->visible)
            {
                continue;
            }

            glm::mat4 inverse = glm::inverse(model->transform);
            glm::vec3 localOrigin = glm::vec3(inverse * glm::vec4(origin, 1.0f));
            glm::vec3 localDirection = glm::vec3(inverse * glm::vec4(direction, 0.0f));

            if (model->brush->shape != BrushShape::Box)
            {
                const MeshData& unit = unitMesh(*model->brush);
                float shapeT = std::numeric_limits<float>::max();
                glm::vec3 shapeNormal(0.0f, 1.0f, 0.0f);
                for (size_t i = 0; i + 2 < unit.indices.size(); i += 3)
                {
                    const Vertex& v0 = unit.vertices[unit.indices[i]];
                    const Vertex& v1 = unit.vertices[unit.indices[i + 1]];
                    const Vertex& v2 = unit.vertices[unit.indices[i + 2]];
                    float t = 0.0f;
                    if (rayTriangle(localOrigin, localDirection, v0.pos, v1.pos, v2.pos, t) && t < shapeT)
                    {
                        shapeT = t;
                        shapeNormal = v0.normal;
                    }
                }

                if (shapeT < bestT)
                {
                    glm::vec3 worldNormal = glm::normalize(glm::transpose(glm::mat3(inverse)) * shapeNormal);
                    bestT = shapeT;
                    hit.hit = true;
                    hit.point = origin + direction * shapeT;
                    hit.normal = worldNormal;
                    hit.baseY = worldNormal.y > 0.99f ? hit.point.y : model->worldMin.y;
                }
                continue;
            }

            const glm::vec3 boxMin(-0.5f, 0.0f, -0.5f);
            const glm::vec3 boxMax(0.5f, 1.0f, 0.5f);

            float tMin = 0.0f;
            float tMax = std::numeric_limits<float>::max();
            int enterAxis = -1;
            float enterSign = 0.0f;
            bool miss = false;

            for (int axis = 0; axis < 3 && !miss; axis++)
            {
                if (std::abs(localDirection[axis]) < 1e-8f)
                {
                    if (localOrigin[axis] < boxMin[axis] || localOrigin[axis] > boxMax[axis])
                    {
                        miss = true;
                    }
                    continue;
                }

                float inv = 1.0f / localDirection[axis];
                float t0 = (boxMin[axis] - localOrigin[axis]) * inv;
                float t1 = (boxMax[axis] - localOrigin[axis]) * inv;
                float sign = -1.0f;
                if (t0 > t1)
                {
                    std::swap(t0, t1);
                    sign = 1.0f;
                }
                if (t0 > tMin)
                {
                    tMin = t0;
                    enterAxis = axis;
                    enterSign = sign;
                }
                tMax = std::min(tMax, t1);
                if (tMin > tMax)
                {
                    miss = true;
                }
            }

            if (miss || enterAxis < 0 || tMin <= 0.0f || tMin >= bestT)
            {
                continue;
            }

            glm::vec3 localNormal(0.0f);
            localNormal[enterAxis] = enterSign;
            glm::vec3 worldNormal = glm::normalize(glm::transpose(glm::mat3(inverse)) * localNormal);

            bestT = tMin;
            hit.hit = true;
            hit.point = origin + direction * tMin;
            hit.normal = worldNormal;
            hit.baseY = worldNormal.y > 0.99f ? hit.point.y : model->worldMin.y;
        }

        return hit.hit;
    }

    void collectSnaps(SnapSets& snaps, bool excludeSelected) const override
    {
        snaps.ys.push_back(0.0f);
        for (const auto& model : m_engine.M_MODELS)
        {
            if (model->ghost || !model->visible)
            {
                continue;
            }
            if (excludeSelected && model.get() == m_engine.m_selected)
            {
                continue;
            }
            snaps.xs.push_back(model->worldMin.x);
            snaps.xs.push_back(model->worldMax.x);
            snaps.ys.push_back(model->worldMin.y);
            snaps.ys.push_back(model->worldMax.y);
            snaps.zs.push_back(model->worldMin.z);
            snaps.zs.push_back(model->worldMax.z);
        }
    }

    bool selectedBrushFrame(glm::vec3& position, float& yawDegrees, glm::vec3& size) override
    {
        Model* model = m_engine.m_selected;
        if (!model || model->ghost || !model->visible || !model->brush)
        {
            return false;
        }

        if (model->scale != 1.0f)
        {
            model->brush->size *= model->scale;
            model->scale = 1.0f;
            m_engine.updateModelTransform(*model);
        }

        position = model->position;
        yawDegrees = model->yawDegrees;
        size = model->brush->size;
        return true;
    }

    void setSelectedBrush(const glm::vec3& position, const glm::vec3& size) override
    {
        Model* model = m_engine.m_selected;
        if (!model || !model->brush)
        {
            return;
        }

        model->position = position;
        model->brush->size = size;
        m_engine.updateModelTransform(*model);
        m_engine.deferShadowUpdate();
    }

    glm::vec3 spawnPoint() const override
    {
        const glm::vec3 origin = m_engine.m_camera.position;
        const glm::vec3 direction = m_engine.m_camera.front();

        if (direction.y < -1e-4f)
        {
            float t = -origin.y / direction.y;
            if (t > 0.0f && t < 100.0f)
            {
                glm::vec3 hit = origin + direction * t;
                return glm::vec3(hit.x, 0.0f, hit.z);
            }
        }

        glm::vec2 flat(direction.x, direction.z);
        float length = glm::length(flat);
        if (length < 1e-4f)
        {
            return glm::vec3(origin.x, 0.0f, origin.z);
        }
        flat = flat / length * 6.0f;
        return glm::vec3(origin.x + flat.x, 0.0f, origin.z + flat.y);
    }

    LevelData captureLevel() const override
    {
        LevelData level;
        std::filesystem::path root(assetRoot());

        for (const auto& model : m_engine.M_MODELS)
        {
            if (model->ghost || model->isGround || !model->asset)
            {
                continue;
            }

            if (model->brush)
            {
                LevelBrush brush;
                brush.name = model->name;
                brush.def = *model->brush;
                brush.def.size *= model->scale;
                brush.position = model->position;
                brush.yawDegrees = model->yawDegrees;
                level.brushes.push_back(brush);
            }
            else
            {
                LevelModel entry;
                entry.name = model->name;
                entry.asset = std::filesystem::path(model->asset->path).lexically_relative(root).generic_string();
                entry.position = model->position;
                entry.yawDegrees = model->yawDegrees;
                entry.scale = model->scale;
                level.models.push_back(entry);
            }
        }

        level.hasSettings = true;
        level.settings = m_engine.m_settings;
        level.hasCamera = true;
        level.camera.position = m_engine.m_camera.position;
        level.camera.yaw = m_engine.m_camera.yaw;
        level.camera.pitch = m_engine.m_camera.pitch;
        return level;
    }

    void applyEnvironment(const LevelData& level) override
    {
        if (level.hasSettings)
        {
            m_engine.m_settings = level.settings;
            m_engine.applySunDirection();
        }
        if (level.hasCamera)
        {
            m_engine.m_camera.position = level.camera.position;
            m_engine.m_camera.yaw = level.camera.yaw;
            m_engine.m_camera.pitch = level.camera.pitch;
        }
        m_engine.m_shadowDirty = true;
    }

    void clearLevel() override
    {
        for (size_t i = m_engine.M_MODELS.size(); i-- > 0;)
        {
            m_engine.removeModel(i);
        }
    }

    bool spawnBrush(const LevelBrush& brush, bool select) override
    {
        try
        {
            int index = acquireBrushAsset(brush.def);
            Model* model = m_engine.addInstance(index, false);
            if (!model)
            {
                return false;
            }
            model->name = brush.name;
            model->brush = brush.def;
            model->position = brush.position;
            model->yawDegrees = brush.yawDegrees;
            m_engine.updateModelTransform(*model);
            m_engine.m_shadowDirty = true;
            if (select)
            {
                m_engine.m_selected = model;
            }
            return true;
        }
        catch (const std::exception& e)
        {
            m_engine.m_loadError = e.what();
            std::cerr << "  [editor] Brush creation failed - " << e.what() << "\n";
            return false;
        }
    }

    bool spawnModel(const LevelModel& entry) override
    {
        std::string fullPath = assetRoot() + "/" + entry.asset;

        int index = -1;
        for (size_t i = 0; i < m_engine.M_LIBRARY.size(); i++)
        {
            if (m_engine.M_LIBRARY[i]->path == fullPath)
            {
                index = static_cast<int>(i);
                break;
            }
        }
        if (index < 0)
        {
            index = m_engine.importAsset(fullPath);
        }
        if (index < 0)
        {
            return false;
        }

        Model* model = m_engine.addInstance(index, false);
        if (!model)
        {
            return false;
        }
        if (!entry.name.empty())
        {
            model->name = entry.name;
        }
        model->position = entry.position;
        model->yawDegrees = entry.yawDegrees;
        model->scale = entry.scale;
        m_engine.updateModelTransform(*model);
        m_engine.m_shadowDirty = true;
        return true;
    }

    BrushDef* selectedBrush() override
    {
        Model* model = m_engine.m_selected;
        if (model && !model->ghost && model->brush)
        {
            return &*model->brush;
        }
        return nullptr;
    }

    void applySelectedBrush() override
    {
        Model* model = m_engine.m_selected;
        if (!model || !model->brush || !model->asset || !model->asset->brush)
        {
            return;
        }

        try
        {
            const BrushDef& def = *model->brush;
            if (!sameAsset(*model->asset->brush, def))
            {
                int users = 0;
                for (const auto& other : m_engine.M_MODELS)
                {
                    if (other->asset == model->asset)
                    {
                        users++;
                    }
                }

                if (users == 1)
                {
                    if (!sameGeometry(*model->asset->brush, def))
                    {
                        BrushDef unit = def;
                        unit.size = glm::vec3(1.0f);
                        MeshData mesh = buildBrushMesh(unit);
                        m_engine.replaceAssetMesh(*model->asset, mesh);
                        *model->asset->brush = unit;
                    }
                    else
                    {
                        updateAssetMaterial(*model->asset, def);
                    }
                }
                else
                {
                    int index = acquireBrushAsset(def);
                    model->asset = m_engine.M_LIBRARY[index];
                    m_engine.m_brushPurgePending = true;
                }
            }

            m_engine.updateModelTransform(*model);
            m_engine.deferShadowUpdate();
        }
        catch (const std::exception& e)
        {
            m_engine.m_loadError = e.what();
            std::cerr << "  [editor] Brush update failed - " << e.what() << "\n";
        }
    }

    bool duplicateSelected(const glm::vec3& offset) override
    {
        Model* source = m_engine.m_selected;
        if (!source || source->ghost || !source->asset)
        {
            return false;
        }

        if (source->brush)
        {
            LevelBrush copy;
            copy.name = source->name + " copy";
            copy.def = *source->brush;
            copy.def.size *= source->scale;
            copy.position = source->position + offset;
            copy.yawDegrees = source->yawDegrees;
            return spawnBrush(copy, true);
        }

        int index = -1;
        for (size_t i = 0; i < m_engine.M_LIBRARY.size(); i++)
        {
            if (m_engine.M_LIBRARY[i] == source->asset)
            {
                index = static_cast<int>(i);
                break;
            }
        }
        if (index < 0)
        {
            return false;
        }

        Model* model = m_engine.addInstance(index, false);
        if (!model)
        {
            return false;
        }
        model->name = source->name + " copy";
        model->position = source->position + offset;
        model->yawDegrees = source->yawDegrees;
        model->scale = source->scale;
        m_engine.updateModelTransform(*model);
        m_engine.m_shadowDirty = true;
        m_engine.m_selected = model;
        return true;
    }

    void rotateSelected(float degrees) override
    {
        Model* model = m_engine.m_selected;
        if (!model || model->ghost)
        {
            return;
        }

        model->yawDegrees += degrees;
        while (model->yawDegrees > 180.0f)
        {
            model->yawDegrees -= 360.0f;
        }
        while (model->yawDegrees < -180.0f)
        {
            model->yawDegrees += 360.0f;
        }
        m_engine.updateModelTransform(*model);
        m_engine.deferShadowUpdate();
    }

    void snapSelectedToGrid(float grid) override
    {
        Model* model = m_engine.m_selected;
        if (!model || grid <= 0.0f)
        {
            return;
        }

        glm::vec3 half(0.0f);
        if (model->brush)
        {
            half = model->brush->size * model->scale * 0.5f;
        }

        model->position.x = std::round((model->position.x - half.x) / grid) * grid + half.x;
        model->position.y = std::round(model->position.y / grid) * grid;
        model->position.z = std::round((model->position.z - half.z) / grid) * grid + half.z;
        m_engine.updateModelTransform(*model);
        m_engine.m_shadowDirty = true;
    }

private:
    const MeshData& unitMesh(const BrushDef& def) const
    {
        int steps = def.shape == BrushShape::Stairs ? def.steps : 0;
        auto key = std::make_pair(static_cast<int>(def.shape), steps);
        auto found = m_unitMeshes.find(key);
        if (found == m_unitMeshes.end())
        {
            BrushDef unit;
            unit.shape = def.shape;
            unit.steps = def.steps;
            found = m_unitMeshes.emplace(key, buildBrushMesh(unit)).first;
        }
        return found->second;
    }

    int acquireBrushAsset(const BrushDef& def)
    {
        for (size_t i = 0; i < m_engine.M_LIBRARY.size(); i++)
        {
            const auto& asset = m_engine.M_LIBRARY[i];
            if (asset->brush && sameAsset(*asset->brush, def))
            {
                return static_cast<int>(i);
            }
        }

        BrushDef unit = def;
        unit.size = glm::vec3(1.0f);
        MeshData mesh = buildBrushMesh(unit);
        int index = m_engine.registerAsset(mesh, "Brush material", std::string());
        m_engine.M_LIBRARY[index]->brush = std::make_shared<BrushDef>(unit);
        return index;
    }

    void updateAssetMaterial(ModelAsset& asset, const BrushDef& def)
    {
        if (asset.materials.empty() || !asset.materialBuffer.mapped)
        {
            return;
        }

        MaterialParams& params = asset.materials[0].params;
        params.baseColorFactor = glm::vec4(def.color, 1.0f);
        params.factors.x = def.metallic;
        params.factors.y = def.roughness;
        std::memcpy(asset.materialBuffer.mapped, &params, sizeof(MaterialParams));

        BrushDef unit = def;
        unit.size = glm::vec3(1.0f);
        *asset.brush = unit;
    }

    BlackGlass& m_engine;
    mutable std::map<std::pair<int, int>, MeshData> m_unitMeshes;
};

void BlackGlass::initEditor()
{
    m_editorHost = std::make_unique<BlackGlassEditorHost>(*this);
    m_editor = std::make_unique<Editor>(*m_editorHost);
}

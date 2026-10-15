#include "../BlackGlass.h"
#include "../Editor/BrushMesh.h"
#include "../Editor/Editor.h"
#include "../Editor/EditorHost.h"
#include "../Editor/ProceduralTextures.h"
#include "../stb_image.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <set>
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
        return a.color == b.color && a.roughness == b.roughness && a.metallic == b.metallic &&
               a.texture == b.texture && a.textureScale == b.textureScale;
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

    int selectionCount() const override
    {
        return static_cast<int>(m_engine.m_selection.size());
    }

    bool selectionBounds(int index, glm::mat4& transform, glm::vec3& localMin, glm::vec3& localMax) const override
    {
        if (index < 0 || index >= selectionCount())
        {
            return false;
        }
        const Model* model = m_engine.m_selection[static_cast<size_t>(index)];
        if (!model || model->ghost || !model->visible || !model->asset)
        {
            return false;
        }
        transform = model->transform;
        localMin = model->asset->localMin;
        localMax = model->asset->localMax;
        return true;
    }

    void clearSelection() override
    {
        m_engine.clearSelection();
    }

    void listItems(std::vector<ItemInfo>& items) const override
    {
        items.clear();
        items.reserve(m_engine.M_MODELS.size());
        for (size_t i = 0; i < m_engine.M_MODELS.size(); i++)
        {
            const Model& model = *m_engine.M_MODELS[i];
            if (model.ghost || model.isGround || !model.asset)
            {
                continue;
            }
            items.emplace_back();
            fillItem(items.back(), model, static_cast<int>(i));
        }
    }

    bool primaryItem(ItemInfo& info) const override
    {
        if (m_engine.m_selection.size() != 1 || !m_engine.m_selected)
        {
            return false;
        }
        for (size_t i = 0; i < m_engine.M_MODELS.size(); i++)
        {
            if (m_engine.M_MODELS[i].get() == m_engine.m_selected)
            {
                fillItem(info, *m_engine.M_MODELS[i], static_cast<int>(i));
                return true;
            }
        }
        return false;
    }

    void selectItem(int id, bool additive) override
    {
        Model* model = modelAt(id);
        if (!model)
        {
            return;
        }
        if (additive)
        {
            m_engine.toggleSelection(model);
        }
        else
        {
            m_engine.selectOnly(model);
        }
    }

    void setItemHidden(int id, bool hidden) override
    {
        Model* model = modelAt(id);
        if (!model)
        {
            return;
        }
        model->hidden = hidden;
        refreshVisibility(*model);
        m_engine.m_shadowDirty = true;
    }

    void setItemLocked(int id, bool locked) override
    {
        Model* model = modelAt(id);
        if (model)
        {
            model->locked = locked;
        }
    }

    void renameItem(int id, const std::string& name) override
    {
        Model* model = modelAt(id);
        if (model && !name.empty())
        {
            model->name = name;
        }
    }

    void setPrimaryTransform(const glm::vec3& position, float yawDegrees, float scale) override
    {
        Model* model = m_engine.m_selected;
        if (!model || m_engine.m_selection.size() != 1 || model->locked)
        {
            return;
        }
        model->position = position;
        model->yawDegrees = yawDegrees;
        if (!model->brush && !model->light)
        {
            model->scale = std::max(scale, 0.001f);
        }
        m_engine.updateModelTransform(*model);
        m_engine.deferShadowUpdate();
    }

    void focusItem(int id) override
    {
        glm::vec3 lo(std::numeric_limits<float>::max());
        glm::vec3 hi(std::numeric_limits<float>::lowest());
        bool any = false;

        auto grow = [&](const Model* model)
        {
            if (!model || model->ghost || !model->asset)
            {
                return;
            }
            lo = glm::min(lo, model->worldMin);
            hi = glm::max(hi, model->worldMax);
            any = true;
        };

        if (id >= 0)
        {
            grow(modelAt(id));
        }
        else
        {
            for (const Model* model : m_engine.m_selection)
            {
                grow(model);
            }
        }
        if (!any)
        {
            return;
        }

        glm::vec3 center = (lo + hi) * 0.5f;
        float radius = std::max(0.5f * glm::length(hi - lo), 0.5f);
        float aspect = m_engine.M_SWAPCHAINEXTENT.width / static_cast<float>(std::max(m_engine.M_SWAPCHAINEXTENT.height, 1u));
        float halfVertical = glm::radians(m_engine.m_fov) * 0.5f;
        float halfHorizontal = std::atan(std::tan(halfVertical) * aspect);
        float distance = radius / std::sin(std::min(halfVertical, halfHorizontal)) * 1.3f;
        m_engine.m_camera.position = center - m_engine.m_camera.front() * distance;
    }

    void frameAll() override
    {
        m_engine.frameScene();
    }

    void deleteSelection() override
    {
        m_engine.deleteSelected();
    }

    void selectAll() override
    {
        m_engine.clearSelection();
        for (const auto& model : m_engine.M_MODELS)
        {
            if (!model->ghost && !model->isGround && model->visible && model->asset)
            {
                m_engine.addToSelection(model.get());
            }
        }
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
            if (model->light)
            {
                continue;
            }
            if (excludeSelected && m_engine.isSelected(model.get()))
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
        if (m_engine.m_selection.size() != 1 || !model || model->ghost || !model->visible || !model->brush || model->locked)
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

            if (model->light)
            {
                LevelLight light;
                light.name = model->name;
                light.def = *model->light;
                light.position = model->position;
                light.selected = m_engine.isSelected(model.get());
                light.hidden = model->hidden;
                light.locked = model->locked;
                level.lights.push_back(light);
            }
            else if (model->brush)
            {
                LevelBrush brush;
                brush.name = model->name;
                brush.def = *model->brush;
                brush.def.size *= model->scale;
                brush.position = model->position;
                brush.yawDegrees = model->yawDegrees;
                brush.selected = m_engine.isSelected(model.get());
                brush.hidden = model->hidden;
                brush.locked = model->locked;
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
                entry.selected = m_engine.isSelected(model.get());
                entry.hidden = model->hidden;
                entry.locked = model->locked;
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
            model->hidden = brush.hidden;
            model->locked = brush.locked;
            refreshVisibility(*model);
            m_engine.updateModelTransform(*model);
            m_engine.m_shadowDirty = true;
            if (select)
            {
                m_engine.addToSelection(model);
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
        model->hidden = entry.hidden;
        model->locked = entry.locked;
        refreshVisibility(*model);
        m_engine.updateModelTransform(*model);
        m_engine.m_shadowDirty = true;
        if (entry.selected)
        {
            m_engine.addToSelection(model);
        }
        return true;
    }

    bool spawnLight(const LevelLight& light, bool select) override
    {
        try
        {
            int index = acquireMarkerAsset();
            Model* model = m_engine.addInstance(index, false);
            if (!model)
            {
                return false;
            }
            model->name = light.name;
            model->light = light.def;
            model->position = light.position;
            model->hidden = light.hidden;
            model->locked = light.locked;
            refreshVisibility(*model);
            m_engine.updateModelTransform(*model);
            if (select)
            {
                m_engine.addToSelection(model);
            }
            return true;
        }
        catch (const std::exception& e)
        {
            m_engine.m_loadError = e.what();
            std::cerr << "  [editor] Light creation failed - " << e.what() << "\n";
            return false;
        }
    }

    LightDef* selectedLight() override
    {
        Model* model = m_engine.m_selected;
        if (m_engine.m_selection.size() == 1 && model && !model->ghost && model->light)
        {
            return &*model->light;
        }
        return nullptr;
    }

    void setLightMarkersVisible(bool visible) override
    {
        m_engine.m_showLightMarkers = visible;
        for (const auto& model : m_engine.M_MODELS)
        {
            if (model->light)
            {
                refreshVisibility(*model);
            }
        }
        if (!visible)
        {
            std::vector<Model*> kept;
            for (Model* model : m_engine.m_selection)
            {
                if (!model->light)
                {
                    kept.push_back(model);
                }
            }
            m_engine.clearSelection();
            for (Model* model : kept)
            {
                m_engine.addToSelection(model);
            }
        }
    }

    std::vector<std::string> textureNames() override
    {
        std::set<std::string> names(proceduralTextureNames().begin(), proceduralTextureNames().end());

        std::error_code error;
        std::filesystem::path directory = std::filesystem::path(assetRoot()) / "textures";
        for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end; it.increment(error))
        {
            if (it->is_regular_file() && isTextureExtension(it->path().extension().string()))
            {
                names.insert(it->path().stem().string());
            }
        }
        return std::vector<std::string>(names.begin(), names.end());
    }

    BrushDef* selectedBrush() override
    {
        Model* model = m_engine.m_selected;
        if (m_engine.m_selection.size() == 1 && model && !model->ghost && model->brush)
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
                    if (!sameGeometry(*model->asset->brush, def) || model->asset->brush->texture != def.texture)
                    {
                        BrushDef unit = def;
                        unit.size = glm::vec3(1.0f);
                        MeshData mesh = buildAssetMesh(unit);
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
        std::vector<Model*> sources = m_engine.m_selection;
        if (sources.empty())
        {
            return false;
        }

        m_engine.clearSelection();
        bool created = false;

        for (Model* source : sources)
        {
            if (!source || source->ghost || !source->asset)
            {
                continue;
            }

            if (source->light)
            {
                LevelLight copy;
                copy.name = source->name + " copy";
                copy.def = *source->light;
                copy.position = source->position + offset;
                created |= spawnLight(copy, true);
                continue;
            }

            if (source->brush)
            {
                LevelBrush copy;
                copy.name = source->name + " copy";
                copy.def = *source->brush;
                copy.def.size *= source->scale;
                copy.position = source->position + offset;
                copy.yawDegrees = source->yawDegrees;
                created |= spawnBrush(copy, true);
                continue;
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
                continue;
            }

            Model* model = m_engine.addInstance(index, false);
            if (!model)
            {
                continue;
            }
            model->name = source->name + " copy";
            model->position = source->position + offset;
            model->yawDegrees = source->yawDegrees;
            model->scale = source->scale;
            m_engine.updateModelTransform(*model);
            m_engine.m_shadowDirty = true;
            m_engine.addToSelection(model);
            created = true;
        }
        return created;
    }

    void rotateSelected(float degrees) override
    {
        if (m_engine.m_selection.empty())
        {
            return;
        }
        m_engine.rotateSelection(degrees);
        m_engine.deferShadowUpdate();
    }

    void snapSelectedToGrid(float grid) override
    {
        if (grid <= 0.0f)
        {
            return;
        }

        for (Model* model : m_engine.m_selection)
        {
            if (model->locked)
            {
                continue;
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
        }
        m_engine.m_shadowDirty = true;
    }

private:
    Model* modelAt(int id) const
    {
        if (id < 0 || id >= static_cast<int>(m_engine.M_MODELS.size()))
        {
            return nullptr;
        }
        Model* model = m_engine.M_MODELS[static_cast<size_t>(id)].get();
        return model->ghost ? nullptr : model;
    }

    void refreshVisibility(Model& model) const
    {
        model.visible = !model.hidden && !(model.light && !m_engine.m_showLightMarkers);
    }

    void fillItem(ItemInfo& info, const Model& model, int id) const
    {
        info.id = id;
        info.name = model.name;
        info.visible = !model.hidden;
        info.locked = model.locked;
        info.selected = m_engine.isSelected(&model);
        info.position = model.position;
        info.yawDegrees = model.yawDegrees;
        info.scale = model.scale;
        info.nativeSize = model.asset ? model.asset->localMax - model.asset->localMin : glm::vec3(0.0f);

        if (model.light)
        {
            info.kind = ItemKind::Light;
        }
        else if (model.brush)
        {
            info.nativeSize = model.brush->size * model.scale;
            switch (model.brush->shape)
            {
                case BrushShape::Box: info.kind = ItemKind::Box; break;
                case BrushShape::Ramp: info.kind = ItemKind::Ramp; break;
                case BrushShape::Stairs: info.kind = ItemKind::Stairs; break;
            }
        }
        else
        {
            info.kind = ItemKind::Model;
            info.nativeSize *= model.scale;
        }
    }

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
        MeshData mesh = buildAssetMesh(unit);
        int index = m_engine.registerAsset(mesh, "Brush material", std::string());
        m_engine.M_LIBRARY[index]->brush = std::make_shared<BrushDef>(unit);
        return index;
    }

    int acquireMarkerAsset()
    {
        for (size_t i = 0; i < m_engine.M_LIBRARY.size(); i++)
        {
            if (m_engine.M_LIBRARY[i]->marker)
            {
                return static_cast<int>(i);
            }
        }

        MeshData mesh = buildLightMarkerMesh();
        int index = m_engine.registerAsset(mesh, "Light marker", std::string());
        m_engine.M_LIBRARY[index]->marker = true;
        return index;
    }

    static bool isTextureExtension(const std::string& extension)
    {
        std::string lower = extension;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return lower == ".png" || lower == ".jpg" || lower == ".jpeg" || lower == ".tga" || lower == ".bmp";
    }

    bool loadTextureFile(const std::string& name, ImageData& out) const
    {
        std::error_code error;
        std::filesystem::path directory = std::filesystem::path(assetRoot()) / "textures";
        for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end; it.increment(error))
        {
            if (!it->is_regular_file() || !isTextureExtension(it->path().extension().string()) ||
                it->path().stem().string() != name)
            {
                continue;
            }

            int width = 0;
            int height = 0;
            int channels = 0;
            unsigned char* pixels = stbi_load(it->path().string().c_str(), &width, &height, &channels, 4);
            if (!pixels)
            {
                return false;
            }
            out.width = width;
            out.height = height;
            out.pixels.assign(pixels, pixels + static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
            stbi_image_free(pixels);
            return true;
        }
        return false;
    }

    const ImageData* textureImage(const std::string& name)
    {
        auto found = m_textureCache.find(name);
        if (found == m_textureCache.end())
        {
            ImageData image;
            if (!loadTextureFile(name, image))
            {
                image = ImageData{};
                generateProceduralTexture(name, image);
            }
            found = m_textureCache.emplace(name, std::move(image)).first;
        }
        return found->second.pixels.empty() ? nullptr : &found->second;
    }

    MeshData buildAssetMesh(const BrushDef& unit)
    {
        const ImageData* image = unit.texture.empty() ? nullptr : textureImage(unit.texture);
        return buildBrushMesh(unit, image);
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
        params.worldUV.y = def.textureScale;
        std::memcpy(asset.materialBuffer.mapped, &params, sizeof(MaterialParams));

        BrushDef unit = def;
        unit.size = glm::vec3(1.0f);
        *asset.brush = unit;
    }

    BlackGlass& m_engine;
    mutable std::map<std::pair<int, int>, MeshData> m_unitMeshes;
    std::map<std::string, ImageData> m_textureCache;
};

void BlackGlass::initEditor()
{
    m_editorHost = std::make_unique<BlackGlassEditorHost>(*this);
    m_editor = std::make_unique<Editor>(*m_editorHost);
}

bool BlackGlass::handleEditorKey(int key, int mods, bool repeat)
{
    if (!m_editor)
    {
        return false;
    }

    m_editor->noteInput();

    const bool command = (mods & (GLFW_MOD_SUPER | GLFW_MOD_CONTROL)) != 0;
    const bool shift = (mods & GLFW_MOD_SHIFT) != 0;

    if (command)
    {
        switch (key)
        {
        case GLFW_KEY_Z:
            return m_editor->execute(shift ? EditorCommand::Redo : EditorCommand::Undo);
        case GLFW_KEY_Y:
            return m_editor->execute(EditorCommand::Redo);
        case GLFW_KEY_D:
            return repeat || m_editor->execute(EditorCommand::Duplicate);
        case GLFW_KEY_A:
            return m_editor->execute(EditorCommand::SelectAll);
        case GLFW_KEY_S:
            return repeat || m_editor->execute(EditorCommand::Save);
        default:
            return false;
        }
    }

    if (repeat || m_mouseCaptured)
    {
        return false;
    }

    switch (key)
    {
    case GLFW_KEY_B:
        return m_editor->execute(EditorCommand::DrawTool);
    case GLFW_KEY_G:
        return m_editor->execute(EditorCommand::ToggleGrid);
    case GLFW_KEY_R:
        return m_editor->execute(EditorCommand::Rotate);
    case GLFW_KEY_L:
        return m_editor->execute(EditorCommand::AddLight);
    case GLFW_KEY_V:
        return m_editor->execute(EditorCommand::SelectTool);
    case GLFW_KEY_F:
        return m_editor->execute(EditorCommand::Focus);
    case GLFW_KEY_F2:
        return m_editor->execute(EditorCommand::Rename);
    default:
        return false;
    }
}

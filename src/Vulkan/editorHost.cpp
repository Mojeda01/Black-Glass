#include "../BlackGlass.h"
#include "../Editor/BrushMesh.h"
#include "../Editor/Editor.h"
#include "../Editor/EditorHost.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>

namespace
{
    bool sameMaterial(const BrushDef& a, const BrushDef& b)
    {
        return a.color == b.color && a.roughness == b.roughness && a.metallic == b.metallic;
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
            if (!sameMaterial(*model->asset->brush, def))
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
                    updateAssetMaterial(*model->asset, def);
                }
                else
                {
                    int index = acquireBrushAsset(def);
                    model->asset = m_engine.M_LIBRARY[index];
                    m_engine.m_brushPurgePending = true;
                }
            }

            m_engine.updateModelTransform(*model);
            m_engine.m_shadowDirty = true;
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
    int acquireBrushAsset(const BrushDef& def)
    {
        for (size_t i = 0; i < m_engine.M_LIBRARY.size(); i++)
        {
            const auto& asset = m_engine.M_LIBRARY[i];
            if (asset->brush && sameMaterial(*asset->brush, def))
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
};

void BlackGlass::initEditor()
{
    m_editorHost = std::make_unique<BlackGlassEditorHost>(*this);
    m_editor = std::make_unique<Editor>(*m_editorHost);
}

#include "../BlackGlass.h"
#include "../Editor/Editor.h"
#include "../Editor/EditorHost.h"

#include <cmath>
#include <filesystem>
#include <iostream>

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

            if (model->asset->brush)
            {
                LevelBrush brush;
                brush.name = model->name;
                brush.def = *model->asset->brush;
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
        return level;
    }

    void clearLevel() override
    {
        for (size_t i = m_engine.M_MODELS.size(); i-- > 0;)
        {
            m_engine.removeModel(i);
        }
    }

    bool spawnBrush(const LevelBrush& brush, const MeshData& mesh, bool select) override
    {
        try
        {
            MeshData copy = mesh;
            int index = m_engine.registerAsset(copy, brush.name, std::string());
            m_engine.M_LIBRARY[index]->brush = std::make_shared<BrushDef>(brush.def);

            Model* model = m_engine.addInstance(index, false);
            if (!model)
            {
                return false;
            }
            model->name = brush.name;
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
        if (model && !model->ghost && model->asset && model->asset->brush)
        {
            return model->asset->brush.get();
        }
        return nullptr;
    }

    void applySelectedBrush(const MeshData& mesh) override
    {
        Model* model = m_engine.m_selected;
        if (!model || !model->asset)
        {
            return;
        }

        try
        {
            MeshData copy = mesh;
            m_engine.replaceAssetMesh(*model->asset, copy);
        }
        catch (const std::exception& e)
        {
            m_engine.m_loadError = e.what();
            std::cerr << "  [editor] Brush update failed - " << e.what() << "\n";
        }
    }

    void snapSelectedToGrid(float grid) override
    {
        Model* model = m_engine.m_selected;
        if (!model || grid <= 0.0f)
        {
            return;
        }

        glm::vec3 half(0.0f);
        if (model->asset && model->asset->brush)
        {
            half = model->asset->brush->size * 0.5f;
        }

        model->position.x = std::round((model->position.x - half.x) / grid) * grid + half.x;
        model->position.y = std::round(model->position.y / grid) * grid;
        model->position.z = std::round((model->position.z - half.z) / grid) * grid + half.z;
        m_engine.updateModelTransform(*model);
        m_engine.m_shadowDirty = true;
    }

private:
    BlackGlass& m_engine;
};

void BlackGlass::initEditor()
{
    m_editorHost = std::make_unique<BlackGlassEditorHost>(*this);
    m_editor = std::make_unique<Editor>(*m_editorHost);
}

#include "Editor.h"
#include "LevelIO.h"

#include "../imgui/imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

Editor::Editor(EditorHost& host)
    : m_host(host)
{
    refreshLevelList();
}

std::string Editor::levelPath(const std::string& name) const
{
    return m_host.assetRoot() + "/levels/" + name + ".json";
}

float Editor::snapCenter(float value, float size) const
{
    float half = size * 0.5f;
    return std::round((value - half) / m_gridSize) * m_gridSize + half;
}

void Editor::refreshLevelList()
{
    namespace fs = std::filesystem;
    m_levelFiles.clear();

    std::error_code error;
    fs::path directory = fs::path(m_host.assetRoot()) / "levels";
    for (fs::directory_iterator it(directory, error), end; !error && it != end; it.increment(error))
    {
        if (it->is_regular_file() && it->path().extension() == ".json")
        {
            m_levelFiles.push_back(it->path().stem().string());
        }
    }
    std::sort(m_levelFiles.begin(), m_levelFiles.end());
}

void Editor::addBrush()
{
    glm::vec3 point = m_host.spawnPoint();

    LevelBrush brush;
    brush.name = "Brush " + std::to_string(m_nextBrush++);
    brush.def = m_newBrush;
    brush.position = glm::vec3(snapCenter(point.x, m_newBrush.size.x), 0.0f,
                               snapCenter(point.z, m_newBrush.size.z));

    if (!m_host.spawnBrush(brush, true))
    {
        m_status = "Could not create the brush";
    }
}

void Editor::saveLevel()
{
    std::string name = m_levelName;
    if (name.empty())
    {
        m_status = "Enter a level name first";
        return;
    }

    std::string error;
    if (saveLevelFile(levelPath(name), m_host.captureLevel(), error))
    {
        m_status = "Saved " + name;
        refreshLevelList();
    }
    else
    {
        m_status = error;
    }
}

void Editor::loadLevel(const std::string& name)
{
    LevelData level;
    std::string error;
    if (!loadLevelFile(levelPath(name), level, error))
    {
        m_status = error;
        return;
    }

    m_host.clearLevel();

    int failed = 0;
    for (const LevelModel& model : level.models)
    {
        if (!m_host.spawnModel(model))
        {
            failed++;
        }
    }
    for (const LevelBrush& brush : level.brushes)
    {
        if (!m_host.spawnBrush(brush, false))
        {
            failed++;
        }
    }

    m_host.applyEnvironment(level);

    m_nextBrush = static_cast<int>(level.brushes.size()) + 1;
    std::snprintf(m_levelName, sizeof(m_levelName), "%s", name.c_str());
    m_status = "Loaded " + name;
    if (failed > 0)
    {
        m_status += " (" + std::to_string(failed) + " items failed to load)";
    }
}

void Editor::drawBrushSection()
{
    ImGui::DragFloat("Grid size", &m_gridSize, 0.05f, 0.05f, 10.0f, "%.2f");
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("New brushes snap to this grid, size fields step by it");
    }

    ImGui::DragFloat3("Size", &m_newBrush.size.x, m_gridSize, m_gridSize, 1000.0f, "%.2f");
    ImGui::ColorEdit3("Color", &m_newBrush.color.x);
    ImGui::SliderFloat("Roughness", &m_newBrush.roughness, 0.05f, 1.0f, "%.2f");
    ImGui::SliderFloat("Metallic", &m_newBrush.metallic, 0.0f, 1.0f, "%.2f");

    if (ImGui::Button("Add box"))
    {
        addBrush();
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Create a box where the camera is looking at the floor");
    }
}

void Editor::drawSelectedBrushSection()
{
    BrushDef* brush = m_host.selectedBrush();
    if (!brush)
    {
        ImGui::TextDisabled("Select a brush to edit its size and material");
        return;
    }

    bool changed = false;
    changed |= ImGui::DragFloat3("Size##selected", &brush->size.x, m_gridSize, m_gridSize, 1000.0f, "%.2f");
    changed |= ImGui::ColorEdit3("Color##selected", &brush->color.x);
    changed |= ImGui::SliderFloat("Roughness##selected", &brush->roughness, 0.05f, 1.0f, "%.2f");
    changed |= ImGui::SliderFloat("Metallic##selected", &brush->metallic, 0.0f, 1.0f, "%.2f");

    if (changed)
    {
        m_host.applySelectedBrush();
    }

    if (ImGui::Button("Snap to grid"))
    {
        m_host.snapSelectedToGrid(m_gridSize);
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate"))
    {
        m_host.duplicateSelected(glm::vec3(m_gridSize, 0.0f, m_gridSize));
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Copy the selected brush, offset by one grid step");
    }
}

void Editor::drawLevelSection()
{
    ImGui::InputText("Name", m_levelName, sizeof(m_levelName));

    if (ImGui::Button("Save level"))
    {
        saveLevel();
    }
    ImGui::SameLine();
    if (ImGui::Button("New level"))
    {
        m_host.clearLevel();
        m_nextBrush = 1;
        m_status = "Cleared the scene";
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh"))
    {
        refreshLevelList();
    }

    if (m_levelFiles.empty())
    {
        ImGui::TextDisabled("No saved levels in assets/levels");
    }
    for (size_t i = 0; i < m_levelFiles.size(); i++)
    {
        ImGui::PushID(static_cast<int>(i));
        ImGui::Text("%s", m_levelFiles[i].c_str());
        ImGui::SameLine();
        if (ImGui::Button("Load"))
        {
            loadLevel(m_levelFiles[i]);
        }
        ImGui::PopID();
    }

    if (!m_status.empty())
    {
        ImGui::TextDisabled("%s", m_status.c_str());
    }
}

void Editor::drawPanel()
{
    if (ImGui::CollapsingHeader("Brushes", ImGuiTreeNodeFlags_DefaultOpen))
    {
        drawBrushSection();
    }
    if (ImGui::CollapsingHeader("Selected brush", ImGuiTreeNodeFlags_DefaultOpen))
    {
        drawSelectedBrushSection();
    }
    if (ImGui::CollapsingHeader("Level", ImGuiTreeNodeFlags_DefaultOpen))
    {
        drawLevelSection();
    }
}

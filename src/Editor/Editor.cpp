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
    brush.name = std::string(BRUSH_SHAPE_NAMES[static_cast<int>(m_newBrush.shape)]) + " " + std::to_string(m_nextBrush++);
    brush.def = m_newBrush;
    brush.position = glm::vec3(snapCenter(point.x, m_newBrush.size.x), 0.0f,
                               snapCenter(point.z, m_newBrush.size.z));

    m_host.clearSelection();
    if (!m_host.spawnBrush(brush, true))
    {
        m_status = "Could not create the brush";
    }
}

void Editor::createBrushFromBounds(const BrushBounds& bounds)
{
    LevelBrush brush;
    brush.name = std::string(BRUSH_SHAPE_NAMES[static_cast<int>(m_newBrush.shape)]) + " " + std::to_string(m_nextBrush++);
    brush.def = m_newBrush;
    brush.def.size = bounds.size;
    brush.position = bounds.center;
    brush.yawDegrees = bounds.yawDegrees;

    m_host.clearSelection();
    if (!m_host.spawnBrush(brush, true))
    {
        m_status = "Could not create the brush";
    }
}

void Editor::drawGrid()
{
    glm::vec3 focus = m_host.spawnPoint();
    glm::vec3 center(std::round(focus.x / m_gridSize) * m_gridSize, 0.0f,
                     std::round(focus.z / m_gridSize) * m_gridSize);

    m_host.debug().grid(center, 40, m_gridSize, 0.002f,
                        glm::vec4(0.8f, 0.8f, 0.8f, 0.10f),
                        glm::vec4(1.0f, 0.3f, 0.3f, 0.30f), 5);
}

void Editor::drawSelection()
{
    glm::vec3 pad(0.002f);
    int count = m_host.selectionCount();
    for (int i = 0; i < count; i++)
    {
        glm::mat4 transform(1.0f);
        glm::vec3 localMin(0.0f);
        glm::vec3 localMax(0.0f);
        if (!m_host.selectionBounds(i, transform, localMin, localMax))
        {
            continue;
        }

        m_host.debug().obb(transform, localMin - pad, localMax + pad, glm::vec4(1.0f, 0.55f, 0.1f, 1.0f), false);
        m_host.debug().obb(transform, localMin - pad, localMax + pad, glm::vec4(1.0f, 0.55f, 0.1f, 0.25f), true);
    }
}

void Editor::update(bool overlayVisible)
{
    ViewInfo view;
    m_host.viewInfo(view);

    glm::vec3 origin(0.0f);
    glm::vec3 direction(0.0f, -1.0f, 0.0f);
    bool hasRay = m_host.cursorRay(origin, direction);
    bool free = m_host.cursorFree();

    SnapSets snaps;
    if (m_tool.armed() || m_handles.dragging())
    {
        m_host.collectSnaps(snaps, m_handles.dragging());
    }

    std::string label;

    SurfaceHit surface;
    if (m_tool.armed() && hasRay && free)
    {
        m_host.pickSurface(origin, direction, surface);
    }

    BrushBounds bounds;
    if (m_tool.update(view, hasRay, origin, direction, free, surface, snaps, m_gridSize,
                      overlayVisible, m_newBrush, m_host.debug(), bounds, label))
    {
        createBrushFromBounds(bounds);
    }

    HandleTarget target;
    if (!m_tool.armed())
    {
        target.valid = m_host.selectedBrushFrame(target.position, target.yawDegrees, target.size);
    }

    HandleEdit edit;
    if (m_handles.update(view, target, free, m_gridSize, snaps, overlayVisible, m_host.debug(), edit, label))
    {
        m_host.setSelectedBrush(edit.position, edit.size);
    }

    m_label = label;

    if (overlayVisible)
    {
        if (m_showGrid)
        {
            drawGrid();
        }
        if (m_showSelection)
        {
            drawSelection();
        }
    }

    trackHistory(overlayVisible);
}

LevelData Editor::captureState() const
{
    LevelData state = m_host.captureLevel();
    state.hasCamera = false;
    return state;
}

void Editor::trackHistory(bool overlayVisible)
{
    if (!m_history.ready())
    {
        m_history.reset(captureState());
        m_historyDirty = false;
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    bool busy = io.MouseDown[0] || io.MouseDown[1] || io.MouseDown[2] ||
                (overlayVisible && ImGui::IsAnyItemActive()) || m_handles.dragging();
    if (busy)
    {
        m_historyDirty = true;
        return;
    }
    if (m_historyDirty)
    {
        flushHistory();
    }
}

void Editor::flushHistory()
{
    m_history.observe(captureState());
    m_historyDirty = false;
}

void Editor::restoreState(const LevelData& state)
{
    m_host.clearLevel();
    for (const LevelModel& model : state.models)
    {
        m_host.spawnModel(model);
    }
    for (const LevelBrush& brush : state.brushes)
    {
        m_host.spawnBrush(brush, brush.selected);
    }
    m_host.applyEnvironment(state);

    m_history.setCurrent(captureState());
    m_historyDirty = false;
}

bool Editor::undo()
{
    flushHistory();
    LevelData target;
    if (!m_history.undo(target))
    {
        m_status = "Nothing to undo";
        return false;
    }
    restoreState(target);
    m_status = "Undo (" + std::to_string(m_history.undoCount()) + " left)";
    return true;
}

bool Editor::redo()
{
    flushHistory();
    LevelData target;
    if (!m_history.redo(target))
    {
        m_status = "Nothing to redo";
        return false;
    }
    restoreState(target);
    m_status = "Redo (" + std::to_string(m_history.redoCount()) + " left)";
    return true;
}

void Editor::toggleDrawTool()
{
    if (m_tool.armed())
    {
        m_tool.disarm();
    }
    else
    {
        m_host.cancelPlacement();
        m_tool.arm();
    }
}

bool Editor::execute(EditorCommand command)
{
    switch (command)
    {
    case EditorCommand::Undo:
        return undo();
    case EditorCommand::Redo:
        return redo();
    case EditorCommand::Duplicate:
        return m_host.duplicateSelected(glm::vec3(m_gridSize, 0.0f, m_gridSize));
    case EditorCommand::SelectAll:
        m_host.selectAll();
        return true;
    case EditorCommand::DrawTool:
        toggleDrawTool();
        return true;
    case EditorCommand::ToggleGrid:
        m_showGrid = !m_showGrid;
        return true;
    case EditorCommand::Rotate:
        if (m_host.selectionCount() == 0)
        {
            return false;
        }
        m_host.rotateSelected(90.0f);
        return true;
    case EditorCommand::Save:
        saveLevel();
        return true;
    }
    return false;
}

void Editor::drawOverlayUi()
{
    if (m_label.empty())
    {
        return;
    }

    ImVec2 mouse = ImGui::GetIO().MousePos;
    ImDrawList* list = ImGui::GetForegroundDrawList();
    ImVec2 position(mouse.x + 18.0f, mouse.y + 18.0f);
    ImVec2 size = ImGui::CalcTextSize(m_label.c_str());
    list->AddRectFilled(ImVec2(position.x - 6.0f, position.y - 3.0f),
                        ImVec2(position.x + size.x + 6.0f, position.y + size.y + 3.0f),
                        IM_COL32(10, 10, 12, 215), 4.0f);
    list->AddText(position, IM_COL32(245, 245, 245, 255), m_label.c_str());
}

bool Editor::onLeftPress()
{
    if (m_tool.armed())
    {
        return m_tool.onPress();
    }
    return m_handles.onPress();
}

void Editor::onLeftRelease()
{
    m_tool.onRelease();
    m_handles.onRelease();
}

bool Editor::cancelTool()
{
    return m_tool.cancel();
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

    int newShape = static_cast<int>(m_newBrush.shape);
    if (ImGui::Combo("Shape", &newShape, BRUSH_SHAPE_NAMES, BRUSH_SHAPE_COUNT))
    {
        m_newBrush.shape = static_cast<BrushShape>(newShape);
    }
    if (m_newBrush.shape == BrushShape::Stairs)
    {
        ImGui::SliderInt("Steps", &m_newBrush.steps, 1, 32);
    }
    ImGui::DragFloat3("Size", &m_newBrush.size.x, 0.05f, 0.05f, 1000.0f, "%.2f");
    ImGui::ColorEdit3("Color", &m_newBrush.color.x);
    ImGui::SliderFloat("Roughness", &m_newBrush.roughness, 0.05f, 1.0f, "%.2f");
    ImGui::SliderFloat("Metallic", &m_newBrush.metallic, 0.0f, 1.0f, "%.2f");

    if (ImGui::Button("Add brush"))
    {
        addBrush();
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Create a brush where the camera is looking at the floor");
    }

    ImGui::SameLine();
    if (ImGui::Button(m_tool.armed() ? "Stop drawing" : "Draw brushes"))
    {
        toggleDrawTool();
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Drag a footprint, release, move the mouse for height, click to confirm. Ramps and stairs rise toward the drag direction (B)");
    }
    if (m_tool.armed())
    {
        ImGui::TextDisabled("Drag a footprint, release, move the mouse for height, click. Esc to stop.");
    }

    ImGui::Checkbox("Show grid", &m_showGrid);
    ImGui::SameLine();
    ImGui::Checkbox("Show selection outline", &m_showSelection);
}

void Editor::drawSelectedBrushSection()
{
    int count = m_host.selectionCount();
    if (count == 0)
    {
        ImGui::TextDisabled("Click a block to select it, Shift-click to add more");
        return;
    }

    BrushDef* brush = count == 1 ? m_host.selectedBrush() : nullptr;
    if (brush)
    {
        drawBrushProperties(*brush);
    }
    else if (count > 1)
    {
        ImGui::Text("%d items selected", count);
    }

    if (ImGui::Button("Snap to grid"))
    {
        m_host.snapSelectedToGrid(m_gridSize);
    }
    ImGui::SameLine();
    if (ImGui::Button("Rotate 90"))
    {
        m_host.rotateSelected(90.0f);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Turn the selection a quarter turn (R), which also changes which way a ramp or stairs rises");
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate"))
    {
        m_host.duplicateSelected(glm::vec3(m_gridSize, 0.0f, m_gridSize));
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Copy the selection, offset by one grid step (Cmd/Ctrl+D)");
    }
}

void Editor::drawBrushProperties(BrushDef& brush)
{
    bool changed = false;
    int shape = static_cast<int>(brush.shape);
    if (ImGui::Combo("Shape##selected", &shape, BRUSH_SHAPE_NAMES, BRUSH_SHAPE_COUNT))
    {
        brush.shape = static_cast<BrushShape>(shape);
        changed = true;
    }
    if (brush.shape == BrushShape::Stairs)
    {
        changed |= ImGui::SliderInt("Steps##selected", &brush.steps, 1, 32);
    }
    changed |= ImGui::DragFloat3("Size##selected", &brush.size.x, 0.05f, 0.05f, 1000.0f, "%.2f");
    changed |= ImGui::ColorEdit3("Color##selected", &brush.color.x);
    changed |= ImGui::SliderFloat("Roughness##selected", &brush.roughness, 0.05f, 1.0f, "%.2f");
    changed |= ImGui::SliderFloat("Metallic##selected", &brush.metallic, 0.0f, 1.0f, "%.2f");

    if (changed)
    {
        m_host.applySelectedBrush();
    }
}

void Editor::drawLevelSection()
{
    if (ImGui::Button("Undo"))
    {
        undo();
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Cmd/Ctrl+Z");
    }
    ImGui::SameLine();
    if (ImGui::Button("Redo"))
    {
        redo();
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Cmd/Ctrl+Shift+Z");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%d undo / %d redo", static_cast<int>(m_history.undoCount()), static_cast<int>(m_history.redoCount()));

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

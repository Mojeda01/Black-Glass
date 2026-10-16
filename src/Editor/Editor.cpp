#include "Editor.h"
#include "LevelIO.h"

#include "../imgui/imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <map>

namespace
{
    constexpr float MARQUEE_MIN_DRAG = 4.0f;
}

Editor::Editor(EditorHost& host)
    : m_host(host)
{
    refreshLevelList();
    refreshTextureList();
    findRecovery();
}

void Editor::refreshTextureList()
{
    m_textureNames = m_host.textureNames();
}

void Editor::addLight(bool spot)
{
    glm::vec3 point = m_host.spawnPoint();

    LevelLight light;
    light.name = std::string(spot ? "Spot " : "Light ") + std::to_string(m_nextLight++);
    light.def.spot = spot;
    light.position = glm::vec3(std::round(point.x / m_gridSize) * m_gridSize, 2.5f,
                               std::round(point.z / m_gridSize) * m_gridSize);

    m_host.clearSelection();
    if (!m_host.spawnLight(light, true))
    {
        m_status = "Could not create the light";
    }
}

void Editor::addSpawn()
{
    glm::vec3 point = m_host.spawnPoint();

    LevelSpawn spawn;
    spawn.name = "Spawn " + std::to_string(m_nextSpawn++);
    spawn.position = glm::vec3(std::round(point.x / m_gridSize) * m_gridSize, 0.0f,
                               std::round(point.z / m_gridSize) * m_gridSize);

    m_host.clearSelection();
    if (!m_host.spawnSpawn(spawn, true))
    {
        m_status = "Could not create the spawn point";
    }
}

std::string Editor::levelPath(const std::string& name) const
{
    return m_host.assetRoot() + "/levels/" + name + ".json";
}

std::string Editor::autosavePath(const std::string& name) const
{
    return m_host.assetRoot() + "/levels/" + name + ".autosave.json";
}

void Editor::markDirty()
{
    if (!m_dirty)
    {
        m_lastAutosave = std::chrono::steady_clock::now();
    }
    m_dirty = true;
    m_autosaveCurrent = false;
}

void Editor::writeAutosave()
{
    if (m_levelName[0] == '\0')
    {
        return;
    }
    std::string error;
    if (saveLevelFile(autosavePath(m_levelName), m_host.captureLevel(), error))
    {
        m_autosaveCurrent = true;
    }
    m_lastAutosave = std::chrono::steady_clock::now();
}

void Editor::tickAutosave()
{
    if (!m_dirty || m_autosaveCurrent || m_historyDirty)
    {
        return;
    }
    constexpr std::chrono::seconds INTERVAL(120);
    if (std::chrono::steady_clock::now() - m_lastAutosave >= INTERVAL)
    {
        writeAutosave();
    }
}

void Editor::removeAutosave(const std::string& name)
{
    std::error_code error;
    std::filesystem::remove(autosavePath(name), error);
}

void Editor::discardAutosave()
{
    removeAutosave(m_levelName);
}

void Editor::findRecovery()
{
    namespace fs = std::filesystem;
    m_recoverName.clear();

    std::error_code error;
    fs::path directory = fs::path(m_host.assetRoot()) / "levels";
    fs::file_time_type newest{};
    for (fs::directory_iterator it(directory, error), end; !error && it != end; it.increment(error))
    {
        if (!it->is_regular_file() || it->path().extension() != ".json")
        {
            continue;
        }
        std::string stem = it->path().stem().string();
        if (stem.size() <= 9 || stem.compare(stem.size() - 9, 9, ".autosave") != 0)
        {
            continue;
        }
        std::string base = stem.substr(0, stem.size() - 9);

        std::error_code timeError;
        fs::file_time_type autosaveTime = fs::last_write_time(it->path(), timeError);
        if (timeError)
        {
            continue;
        }
        fs::path saved = fs::path(levelPath(base));
        if (fs::exists(saved, timeError))
        {
            fs::file_time_type savedTime = fs::last_write_time(saved, timeError);
            if (!timeError && savedTime >= autosaveTime)
            {
                fs::remove(it->path(), timeError);
                continue;
            }
        }
        if (m_recoverName.empty() || autosaveTime > newest)
        {
            m_recoverName = base;
            newest = autosaveTime;
        }
    }
    m_requestRecover = !m_recoverName.empty();
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
            std::string stem = it->path().stem().string();
            if (stem.size() > 9 && stem.compare(stem.size() - 9, 9, ".autosave") == 0)
            {
                continue;
            }
            m_levelFiles.push_back(stem);
        }
    }
    std::sort(m_levelFiles.begin(), m_levelFiles.end());
}

bool Editor::clipboardEmpty() const
{
    return m_clipboard.brushes.empty() && m_clipboard.models.empty() &&
           m_clipboard.lights.empty() && m_clipboard.spawns.empty();
}

bool Editor::copySelection()
{
    if (m_host.selectionCount() == 0)
    {
        return false;
    }
    m_clipboard = m_host.captureSelection();
    if (clipboardEmpty())
    {
        return false;
    }
    int count = static_cast<int>(m_clipboard.brushes.size() + m_clipboard.models.size() +
                                 m_clipboard.lights.size() + m_clipboard.spawns.size());
    m_status = "Copied " + std::to_string(count) + (count == 1 ? " item" : " items");
    return true;
}

bool Editor::pasteClipboard()
{
    if (clipboardEmpty())
    {
        m_status = "Clipboard is empty";
        return false;
    }

    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(std::numeric_limits<float>::lowest());
    auto grow = [&](const glm::vec3& position)
    {
        lo = glm::min(lo, position);
        hi = glm::max(hi, position);
    };
    for (const LevelBrush& item : m_clipboard.brushes)
    {
        grow(item.position);
    }
    for (const LevelModel& item : m_clipboard.models)
    {
        grow(item.position);
    }
    for (const LevelLight& item : m_clipboard.lights)
    {
        grow(item.position);
    }
    for (const LevelSpawn& item : m_clipboard.spawns)
    {
        grow(item.position);
    }
    glm::vec3 center = (lo + hi) * 0.5f;

    glm::vec3 target = m_host.spawnPoint();
    float deltaY = 0.0f;
    glm::vec3 origin(0.0f);
    glm::vec3 direction(0.0f, -1.0f, 0.0f);
    SurfaceHit surface;
    if (m_host.cursorFree() && m_host.cursorRay(origin, direction) &&
        m_host.pickSurface(origin, direction, surface))
    {
        target = surface.point;
        deltaY = std::round((surface.point.y - lo.y) / m_gridSize) * m_gridSize;
    }

    glm::vec3 delta(std::round((target.x - center.x) / m_gridSize) * m_gridSize,
                    deltaY,
                    std::round((target.z - center.z) / m_gridSize) * m_gridSize);

    std::map<int, int> groupMap;
    auto remap = [&](int group)
    {
        if (group == 0)
        {
            return 0;
        }
        auto found = groupMap.find(group);
        if (found == groupMap.end())
        {
            found = groupMap.emplace(group, m_host.allocateGroup()).first;
        }
        return found->second;
    };

    m_host.clearSelection();
    bool created = false;
    for (LevelBrush item : m_clipboard.brushes)
    {
        item.position += delta;
        item.hidden = false;
        item.group = remap(item.group);
        created |= m_host.spawnBrush(item, true);
    }
    for (LevelModel item : m_clipboard.models)
    {
        item.position += delta;
        item.hidden = false;
        item.selected = true;
        item.group = remap(item.group);
        created |= m_host.spawnModel(item);
    }
    for (LevelLight item : m_clipboard.lights)
    {
        item.position += delta;
        item.hidden = false;
        item.group = remap(item.group);
        created |= m_host.spawnLight(item, true);
    }
    for (LevelSpawn item : m_clipboard.spawns)
    {
        item.position += delta;
        item.hidden = false;
        item.group = remap(item.group);
        created |= m_host.spawnSpawn(item, true);
    }
    if (created)
    {
        m_status = "Pasted";
    }
    return created;
}

void Editor::addBrush(BrushShape shape)
{
    glm::vec3 point = m_host.spawnPoint();

    LevelBrush brush;
    brush.def = m_newBrush;
    brush.def.shape = shape;
    brush.name = std::string(BRUSH_SHAPE_NAMES[static_cast<int>(shape)]) + " " + std::to_string(m_nextBrush++);
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
    m_host.setVerticalSnap(m_gridSize);

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

    bool handlesBusy = m_handles.hovering() || m_handles.dragging();
    bool shiftHeld = ImGui::GetIO().KeyShift;

    TranslateTarget arrows;
    float radius = 0.0f;
    if (m_showMoveGizmo && !m_tool.armed() && !handlesBusy)
    {
        arrows.valid = m_host.rotationFrame(arrows.center, radius);
    }
    glm::vec3 moveStep(0.0f);
    if (m_move.update(view, arrows, hasRay, origin, direction, free && !handlesBusy,
                      shiftHeld ? 0.0f : m_gridSize, overlayVisible, m_host.debug(), moveStep, label))
    {
        m_host.translateSelected(moveStep);
    }
    bool moveBusy = m_move.hovering() || m_move.dragging();

    RotationTarget ring;
    if (!m_tool.armed() && !handlesBusy && !moveBusy)
    {
        ring.valid = m_host.rotationFrame(ring.center, ring.radius);
    }
    float yawStep = 0.0f;
    if (m_gizmo.update(view, ring, hasRay, origin, direction, free && !handlesBusy && !moveBusy,
                       shiftHeld ? 0.0f : m_rotateSnap, overlayVisible, m_host.debug(), yawStep, label))
    {
        m_host.rotateSelected(yawStep);
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
        m_host.drawSpawnMarkers();
        if (m_showLightMarkers)
        {
            m_host.drawLightCones();
        }
    }

    trackHistory(overlayVisible);
    tickAutosave();
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
                (overlayVisible && ImGui::IsAnyItemActive()) || m_handles.dragging() || m_gizmo.dragging() ||
                m_move.dragging();
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
    if (m_history.observe(captureState()))
    {
        markDirty();
    }
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
    for (const LevelLight& light : state.lights)
    {
        m_host.spawnLight(light, light.selected);
    }
    for (const LevelSpawn& spawn : state.spawns)
    {
        m_host.spawnSpawn(spawn, spawn.selected);
    }
    m_host.applyEnvironment(state);

    m_history.setCurrent(captureState());
    m_historyDirty = false;
    markDirty();
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
    case EditorCommand::Copy:
        return copySelection();
    case EditorCommand::Cut:
        if (!copySelection())
        {
            return false;
        }
        m_host.deleteSelection();
        return true;
    case EditorCommand::Paste:
        return pasteClipboard();
    case EditorCommand::Group:
    {
        int count = m_host.groupSelection();
        if (count == 0)
        {
            m_status = "Select at least two unlocked items to group";
            return false;
        }
        m_status = "Grouped " + std::to_string(count) + " items";
        return true;
    }
    case EditorCommand::Ungroup:
    {
        int count = m_host.ungroupSelection();
        if (count == 0)
        {
            m_status = "Nothing to ungroup";
            return false;
        }
        m_status = "Ungrouped " + std::to_string(count) + (count == 1 ? " item" : " items");
        return true;
    }
    case EditorCommand::ViewPerspective:
        m_host.setViewMode(0);
        m_status = "Perspective view";
        return true;
    case EditorCommand::ViewTop:
        m_host.setViewMode(1);
        m_status = "Top view. Right-drag or WASD pans, scroll zooms";
        return true;
    case EditorCommand::ViewFront:
        m_host.setViewMode(2);
        m_status = "Front view. Right-drag or WASD pans, scroll zooms";
        return true;
    case EditorCommand::ViewRight:
        m_host.setViewMode(3);
        m_status = "Right view. Right-drag or WASD pans, scroll zooms";
        return true;
    case EditorCommand::SelectAll:
        m_host.selectAll();
        return true;
    case EditorCommand::DrawTool:
        toggleDrawTool();
        return true;
    case EditorCommand::SelectTool:
        m_tool.disarm();
        return true;
    case EditorCommand::Focus:
        m_host.focusItem(-1);
        return true;
    case EditorCommand::Rename:
        if (m_host.selectionCount() != 1)
        {
            return false;
        }
        m_focusName = true;
        return true;
    case EditorCommand::Delete:
        m_host.deleteSelection();
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
    case EditorCommand::ToggleMoveGizmo:
        m_showMoveGizmo = !m_showMoveGizmo;
        return true;
    case EditorCommand::Save:
        saveLevel();
        return true;
    case EditorCommand::AddLight:
        addLight();
        return true;
    }
    return false;
}

void Editor::drawOverlayUi()
{
    if (m_marqueeActive)
    {
        ImVec2 mouse = ImGui::GetIO().MousePos;
        ImVec2 lo(std::min(m_marqueeStart.x, mouse.x), std::min(m_marqueeStart.y, mouse.y));
        ImVec2 hi(std::max(m_marqueeStart.x, mouse.x), std::max(m_marqueeStart.y, mouse.y));
        if (hi.x - lo.x >= MARQUEE_MIN_DRAG || hi.y - lo.y >= MARQUEE_MIN_DRAG)
        {
            ImDrawList* list = ImGui::GetForegroundDrawList();
            list->AddRectFilled(lo, hi, IM_COL32(255, 140, 26, 40));
            list->AddRect(lo, hi, IM_COL32(255, 140, 26, 230));
        }
    }

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
    if (m_handles.onPress())
    {
        return true;
    }
    if (m_move.onPress())
    {
        return true;
    }
    if (m_gizmo.onPress())
    {
        return true;
    }

    ViewInfo view;
    if (m_host.viewInfo(view) && m_host.emptySpaceUnderCursor())
    {
        m_marqueeActive = true;
        m_marqueeStart = view.cursor;
        m_marqueeAdditive = ImGui::GetIO().KeyShift;
    }
    return false;
}

void Editor::onLeftRelease()
{
    m_tool.onRelease();
    m_handles.onRelease();
    m_move.onRelease();
    m_gizmo.onRelease();

    if (!m_marqueeActive)
    {
        return;
    }
    m_marqueeActive = false;

    ViewInfo view;
    if (!m_host.viewInfo(view))
    {
        return;
    }
    glm::vec2 lo = glm::min(m_marqueeStart, view.cursor);
    glm::vec2 hi = glm::max(m_marqueeStart, view.cursor);
    if (hi.x - lo.x < MARQUEE_MIN_DRAG && hi.y - lo.y < MARQUEE_MIN_DRAG)
    {
        return;
    }
    m_host.selectInRect(lo, hi, m_marqueeAdditive);
    int count = m_host.selectionCount();
    m_status = std::to_string(count) + (count == 1 ? " item selected" : " items selected");
}

bool Editor::cancelTool()
{
    return m_tool.cancel();
}

bool Editor::saveCurrent()
{
    return saveLevel();
}

bool Editor::saveLevel()
{
    std::string name = m_levelName;
    if (name.empty())
    {
        m_status = "Enter a level name first";
        return false;
    }

    std::string error;
    flushHistory();
    if (saveLevelFile(levelPath(name), m_host.captureLevel(), error))
    {
        m_status = "Saved " + name;
        m_dirty = false;
        m_autosaveCurrent = true;
        removeAutosave(name);
        refreshLevelList();
        return true;
    }
    m_status = error;
    return false;
}

bool Editor::applyLevel(const std::string& path, const std::string& name, bool dirty)
{
    LevelData level;
    std::string error;
    if (!loadLevelFile(path, level, error))
    {
        m_status = error;
        return false;
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
    for (const LevelLight& light : level.lights)
    {
        if (!m_host.spawnLight(light, false))
        {
            failed++;
        }
    }
    for (const LevelSpawn& spawn : level.spawns)
    {
        if (!m_host.spawnSpawn(spawn, false))
        {
            failed++;
        }
    }

    m_host.applyEnvironment(level);

    m_nextBrush = static_cast<int>(level.brushes.size()) + 1;
    m_nextLight = static_cast<int>(level.lights.size()) + 1;
    m_nextSpawn = static_cast<int>(level.spawns.size()) + 1;
    std::snprintf(m_levelName, sizeof(m_levelName), "%s", name.c_str());
    flushHistory();
    m_dirty = false;
    m_autosaveCurrent = true;
    if (dirty)
    {
        markDirty();
    }
    m_status = "Loaded " + name;
    if (failed > 0)
    {
        m_status += " (" + std::to_string(failed) + " items failed to load)";
    }
    return true;
}

void Editor::loadLevel(const std::string& name)
{
    removeAutosave(m_levelName);
    applyLevel(levelPath(name), name, false);
}

void Editor::newLevel()
{
    removeAutosave(m_levelName);
    m_host.clearLevel();
    m_nextBrush = 1;
    m_nextLight = 1;
    m_nextSpawn = 1;
    flushHistory();
    m_dirty = false;
    m_autosaveCurrent = true;
    m_status = "Cleared the scene";
}

void Editor::requestNewLevel()
{
    m_pending = PendingAction::New;
    if (m_dirty)
    {
        m_requestUnsaved = true;
        return;
    }
    performPending();
}

void Editor::requestLoadLevel(const std::string& name)
{
    m_pending = PendingAction::Open;
    m_pendingLevel = name;
    if (m_dirty)
    {
        m_requestUnsaved = true;
        return;
    }
    performPending();
}

void Editor::performPending()
{
    PendingAction action = m_pending;
    m_pending = PendingAction::None;
    if (action == PendingAction::New)
    {
        newLevel();
    }
    else if (action == PendingAction::Open)
    {
        loadLevel(m_pendingLevel);
    }
}

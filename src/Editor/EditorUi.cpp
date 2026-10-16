#include "Editor.h"

#include "../imgui/imgui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
    constexpr float LEFT_WIDTH = 290.0f;
    constexpr float RIGHT_WIDTH = 340.0f;

    constexpr ImGuiWindowFlags PANEL_FLAGS = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                                             ImGuiWindowFlags_NoBringToFrontOnFocus;

    bool toggleButton(const char* label, bool active, const ImVec2& size = ImVec2(0.0f, 0.0f))
    {
        if (active)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
        }
        bool pressed = ImGui::Button(label, size);
        if (active)
        {
            ImGui::PopStyleColor();
        }
        return pressed;
    }

    void hint(const char* text)
    {
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", text);
        }
    }

    void wrappedHint(const char* text)
    {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", text);
        ImGui::PopTextWrapPos();
    }

    const char* kindTag(ItemKind kind)
    {
        switch (kind)
        {
            case ItemKind::Box: return "BOX";
            case ItemKind::Ramp: return "RAMP";
            case ItemKind::Stairs: return "STAIRS";
            case ItemKind::Wedge: return "WEDGE";
            case ItemKind::Cylinder: return "CYLINDER";
            case ItemKind::Model: return "MODEL";
            case ItemKind::Light: return "LIGHT";
            case ItemKind::Spawn: return "SPAWN";
        }
        return "";
    }

    bool containsNoCase(const std::string& text, const char* query)
    {
        if (query[0] == '\0')
        {
            return true;
        }
        std::string haystack = text;
        std::string needle = query;
        auto lower = [](std::string& value)
        {
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        };
        lower(haystack);
        lower(needle);
        return haystack.find(needle) != std::string::npos;
    }
}

void Editor::drawFileMenu()
{
    if (ImGui::MenuItem("New level"))
    {
        requestNewLevel();
    }

    if (ImGui::BeginMenu("Open level"))
    {
        refreshLevelList();
        if (m_levelFiles.empty())
        {
            ImGui::TextDisabled("No saved levels");
        }
        for (const std::string& name : m_levelFiles)
        {
            if (ImGui::MenuItem(name.c_str()))
            {
                requestLoadLevel(name);
            }
        }
        ImGui::EndMenu();
    }

    ImGui::Separator();
    if (ImGui::MenuItem("Save", "Cmd/Ctrl+S"))
    {
        saveLevel();
    }
    if (ImGui::MenuItem("Save as..."))
    {
        std::snprintf(m_saveAsName, sizeof(m_saveAsName), "%s", m_levelName);
        m_requestSaveAs = true;
    }
}

void Editor::drawEditMenu()
{
    if (ImGui::MenuItem("Undo", "Cmd/Ctrl+Z", false, m_history.undoCount() > 0))
    {
        undo();
    }
    if (ImGui::MenuItem("Redo", "Cmd/Ctrl+Shift+Z", false, m_history.redoCount() > 0))
    {
        redo();
    }

    ImGui::Separator();
    bool hasSelection = m_host.selectionCount() > 0;
    if (ImGui::MenuItem("Cut", "Cmd/Ctrl+X", false, hasSelection))
    {
        execute(EditorCommand::Cut);
    }
    if (ImGui::MenuItem("Copy", "Cmd/Ctrl+C", false, hasSelection))
    {
        execute(EditorCommand::Copy);
    }
    if (ImGui::MenuItem("Paste", "Cmd/Ctrl+V", false, !clipboardEmpty()))
    {
        execute(EditorCommand::Paste);
    }
    if (ImGui::MenuItem("Duplicate", "Cmd/Ctrl+D", false, hasSelection))
    {
        execute(EditorCommand::Duplicate);
    }
    if (ImGui::MenuItem("Delete", "Del", false, hasSelection))
    {
        m_host.deleteSelection();
    }
    if (ImGui::MenuItem("Rotate 90", "R", false, hasSelection))
    {
        m_host.rotateSelected(90.0f);
    }
    if (ImGui::MenuItem("Select all", "Cmd/Ctrl+A"))
    {
        m_host.selectAll();
    }
    if (ImGui::MenuItem("Deselect", nullptr, false, hasSelection))
    {
        m_host.clearSelection();
    }
}

void Editor::drawAddMenu()
{
    if (ImGui::MenuItem("Box"))
    {
        addBrush(BrushShape::Box);
    }
    if (ImGui::MenuItem("Ramp"))
    {
        addBrush(BrushShape::Ramp);
    }
    if (ImGui::MenuItem("Stairs"))
    {
        addBrush(BrushShape::Stairs);
    }
    if (ImGui::MenuItem("Wedge"))
    {
        addBrush(BrushShape::Wedge);
    }
    if (ImGui::MenuItem("Cylinder"))
    {
        addBrush(BrushShape::Cylinder);
    }
    if (ImGui::MenuItem("Light", "L"))
    {
        addLight();
    }
    if (ImGui::MenuItem("Player spawn"))
    {
        addSpawn();
    }
}

void Editor::drawViewMenu()
{
    ImGui::MenuItem("Tools panel", nullptr, &m_showTools);
    ImGui::MenuItem("Outliner", nullptr, &m_showOutliner);
    ImGui::MenuItem("Inspector", nullptr, &m_showInspector);

    ImGui::Separator();
    ImGui::MenuItem("Grid", "G", &m_showGrid);
    ImGui::MenuItem("Selection outline", nullptr, &m_showSelection);
    ImGui::MenuItem("Move gizmo", "T", &m_showMoveGizmo);
    if (ImGui::MenuItem("Light markers", nullptr, &m_showLightMarkers))
    {
        m_host.setLightMarkersVisible(m_showLightMarkers);
    }

    ImGui::Separator();
    if (ImGui::MenuItem("Frame selection", "F", false, m_host.selectionCount() > 0))
    {
        m_host.focusItem(-1);
    }
    if (ImGui::MenuItem("Frame all"))
    {
        m_host.frameAll();
    }
}

void Editor::drawPanels(float framesPerSecond)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float menuHeight = ImGui::GetFrameHeight();
    const ImVec2 origin(viewport->Pos.x, viewport->Pos.y + menuHeight);
    const ImVec2 size(viewport->Size.x, viewport->Size.y - menuHeight);

    float statusHeight = ImGui::GetFrameHeight() + style.WindowPadding.y * 2.0f;
    float panelHeight = std::max(size.y - statusHeight, 100.0f);

    if (m_showOutliner || m_showInspector)
    {
        m_host.listItems(m_items);
    }

    float toolsHeight = m_showOutliner ? std::floor(panelHeight * 0.56f) : panelHeight;
    float outlinerHeight = m_showTools ? panelHeight - toolsHeight : panelHeight;
    float outlinerTop = origin.y + (m_showTools ? toolsHeight : 0.0f);

    if (m_showTools)
    {
        ImGui::SetNextWindowPos(origin);
        ImGui::SetNextWindowSize(ImVec2(LEFT_WIDTH, toolsHeight));
        ImGui::SetNextWindowBgAlpha(0.94f);
        if (ImGui::Begin("Tools", &m_showTools, PANEL_FLAGS))
        {
            drawToolsPanel();
        }
        ImGui::End();
    }

    if (m_showOutliner)
    {
        ImGui::SetNextWindowPos(ImVec2(origin.x, outlinerTop));
        ImGui::SetNextWindowSize(ImVec2(LEFT_WIDTH, outlinerHeight));
        ImGui::SetNextWindowBgAlpha(0.94f);
        if (ImGui::Begin("Outliner", &m_showOutliner, PANEL_FLAGS))
        {
            drawOutlinerPanel();
        }
        ImGui::End();
    }

    if (m_showInspector)
    {
        ImGui::SetNextWindowPos(ImVec2(origin.x + size.x - RIGHT_WIDTH, origin.y));
        ImGui::SetNextWindowSize(ImVec2(RIGHT_WIDTH, panelHeight));
        ImGui::SetNextWindowBgAlpha(0.94f);
        if (ImGui::Begin("Inspector", &m_showInspector, PANEL_FLAGS))
        {
            drawInspectorPanel();
        }
        ImGui::End();
    }

    ImGui::SetNextWindowPos(ImVec2(origin.x, origin.y + size.y - statusHeight));
    ImGui::SetNextWindowSize(ImVec2(size.x, statusHeight));
    drawStatusBar(framesPerSecond);

    drawSaveAsPopup();
    drawUnsavedPopup();
    drawRecoverPopup();
}

void Editor::drawToolsPanel()
{
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float half = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;

    if (toggleButton("Select  (V)", !m_tool.armed(), ImVec2(half, 0.0f)))
    {
        m_tool.disarm();
    }
    hint("Click blocks to select them. Shift-click adds to the selection. Alt-drag moves the selection up and down");
    ImGui::SameLine();
    if (toggleButton("Draw  (B)", m_tool.armed(), ImVec2(half, 0.0f)))
    {
        if (!m_tool.armed())
        {
            toggleDrawTool();
        }
    }
    hint("Drag a footprint on the floor, release, move the mouse for height, click to confirm");

    if (ImGui::Button("Add light  (L)", ImVec2(-1.0f, 0.0f)))
    {
        addLight();
    }
    hint("Place a point light 2.5 m above where the camera is looking");

    if (ImGui::Button("Add player spawn", ImVec2(-1.0f, 0.0f)))
    {
        addSpawn();
    }
    hint("Where walk mode (P) starts. Rotate it to set the facing direction");

    if (m_tool.armed())
    {
        wrappedHint("Drag a footprint, release, move the mouse for height, then click. Esc stops drawing.");
    }
    else
    {
        wrappedHint("Click to select, Shift-click to add, drag to move, Alt-drag to lift, drag the ring to rotate.");
    }

    ImGui::SeparatorText("Grid");
    ImGui::SetNextItemWidth(120.0f);
    ImGui::DragFloat("Size", &m_gridSize, 0.05f, 0.05f, 10.0f, "%.2f");
    hint("New brushes and moves snap to this grid");
    ImGui::SetNextItemWidth(120.0f);
    ImGui::DragFloat("Rotate snap", &m_rotateSnap, 1.0f, 1.0f, 90.0f, "%.0f deg");
    hint("The ring rotates in steps of this size. Hold Shift for free rotation");
    ImGui::Checkbox("Show grid", &m_showGrid);
    ImGui::SameLine();
    ImGui::Checkbox("Outline", &m_showSelection);

    ImGui::SeparatorText("New brush");
    int shape = static_cast<int>(m_newBrush.shape);
    if (ImGui::Combo("Shape", &shape, BRUSH_SHAPE_NAMES, BRUSH_SHAPE_COUNT))
    {
        m_newBrush.shape = static_cast<BrushShape>(shape);
    }
    if (m_newBrush.shape == BrushShape::Stairs)
    {
        ImGui::SliderInt("Steps", &m_newBrush.steps, 1, 32);
    }
    else if (m_newBrush.shape == BrushShape::Cylinder)
    {
        m_newBrush.steps = std::clamp(m_newBrush.steps, 3, 48);
        ImGui::SliderInt("Sides", &m_newBrush.steps, 3, 48);
    }
    ImGui::DragFloat3("Size##new", &m_newBrush.size.x, 0.05f, 0.05f, 1000.0f, "%.2f");
    hint("Size used by the Add buttons. The Draw tool takes its size from your drag");
    ImGui::ColorEdit3("Color##new", &m_newBrush.color.x);
    ImGui::SliderFloat("Roughness##new", &m_newBrush.roughness, 0.05f, 1.0f, "%.2f");
    ImGui::SliderFloat("Metallic##new", &m_newBrush.metallic, 0.0f, 1.0f, "%.2f");
    drawTextureControls(m_newBrush, "##new");

    float third = (ImGui::GetContentRegionAvail().x - spacing * 2.0f) / 3.0f;
    if (ImGui::Button("+ Box", ImVec2(third, 0.0f)))
    {
        addBrush(BrushShape::Box);
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Ramp", ImVec2(third, 0.0f)))
    {
        addBrush(BrushShape::Ramp);
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Stairs", ImVec2(third, 0.0f)))
    {
        addBrush(BrushShape::Stairs);
    }
    hint("Create a brush with these settings where the camera is looking");
    if (ImGui::Button("+ Wedge", ImVec2(half, 0.0f)))
    {
        addBrush(BrushShape::Wedge);
    }
    hint("Triangular prism, useful for corners and diagonal cuts");
    ImGui::SameLine();
    if (ImGui::Button("+ Cylinder", ImVec2(half, 0.0f)))
    {
        addBrush(BrushShape::Cylinder);
    }
    hint("Round column. Sides are set with the slider above");
}

void Editor::drawOutlinerPanel()
{
    int total = static_cast<int>(m_items.size());
    int selected = m_host.selectionCount();
    ImGui::Text("%d items", total);
    ImGui::SameLine();
    ImGui::TextDisabled("%d selected", selected);

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##outlinerfilter", "Search...", m_outlinerFilter, sizeof(m_outlinerFilter));

    std::vector<int> rows;
    rows.reserve(m_items.size());
    for (size_t i = 0; i < m_items.size(); i++)
    {
        std::string haystack = std::string(kindTag(m_items[i].kind)) + " " + m_items[i].name;
        if (containsNoCase(haystack, m_outlinerFilter))
        {
            rows.push_back(static_cast<int>(i));
        }
    }

    int deleteAfter = -1;
    int duplicateAfter = -1;

    ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit |
                            ImGuiTableFlags_BordersInnerV;
    if (ImGui::BeginTable("##items", 3, flags, ImVec2(0.0f, ImGui::GetContentRegionAvail().y)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Show", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("Lock", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()));
        while (clipper.Step())
        {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
            {
                const ItemInfo& item = m_items[static_cast<size_t>(rows[static_cast<size_t>(row)])];

                ImGui::TableNextRow();
                ImGui::PushID(item.id);

                bool dimmed = !item.visible;
                if (dimmed)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                }

                ImGui::TableSetColumnIndex(0);
                std::string label = std::string(kindTag(item.kind)) + "   " + item.name;
                if (ImGui::Selectable(label.c_str(), item.selected, ImGuiSelectableFlags_AllowDoubleClick))
                {
                    if (ImGui::IsMouseDoubleClicked(0))
                    {
                        m_host.focusItem(item.id);
                    }
                    else
                    {
                        const ImGuiIO& io = ImGui::GetIO();
                        m_host.selectItem(item.id, io.KeyShift || io.KeyCtrl || io.KeySuper);
                    }
                }

                if (ImGui::BeginPopupContextItem("##rowmenu"))
                {
                    if (ImGui::MenuItem("Rename"))
                    {
                        m_host.selectItem(item.id, false);
                        m_focusName = true;
                    }
                    if (ImGui::MenuItem("Focus camera"))
                    {
                        m_host.focusItem(item.id);
                    }
                    if (ImGui::MenuItem("Duplicate"))
                    {
                        m_host.selectItem(item.id, false);
                        duplicateAfter = item.id;
                    }
                    if (ImGui::MenuItem("Delete"))
                    {
                        m_host.selectItem(item.id, false);
                        deleteAfter = item.id;
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem(item.visible ? "Hide" : "Show"))
                    {
                        m_host.setItemHidden(item.id, item.visible);
                    }
                    if (ImGui::MenuItem(item.locked ? "Unlock" : "Lock"))
                    {
                        m_host.setItemLocked(item.id, !item.locked);
                    }
                    ImGui::EndPopup();
                }

                if (dimmed)
                {
                    ImGui::PopStyleColor();
                }

                ImGui::TableSetColumnIndex(1);
                bool shown = item.visible;
                if (ImGui::Checkbox("##show", &shown))
                {
                    m_host.setItemHidden(item.id, !shown);
                }

                ImGui::TableSetColumnIndex(2);
                bool locked = item.locked;
                if (ImGui::Checkbox("##lock", &locked))
                {
                    m_host.setItemLocked(item.id, locked);
                }

                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }

    if (duplicateAfter >= 0)
    {
        execute(EditorCommand::Duplicate);
    }
    if (deleteAfter >= 0)
    {
        m_host.deleteSelection();
    }
}

void Editor::drawInspectorPanel()
{
    int count = m_host.selectionCount();

    if (count == 0)
    {
        ImGui::TextDisabled("Nothing selected");
        wrappedHint("Click a block, light or model in the view, or pick one in the Outliner, to edit it here.");

        int boxes = 0;
        int ramps = 0;
        int stairs = 0;
        int wedges = 0;
        int cylinders = 0;
        int models = 0;
        int lights = 0;
        for (const ItemInfo& item : m_items)
        {
            switch (item.kind)
            {
                case ItemKind::Box: boxes++; break;
                case ItemKind::Ramp: ramps++; break;
                case ItemKind::Stairs: stairs++; break;
                case ItemKind::Wedge: wedges++; break;
                case ItemKind::Cylinder: cylinders++; break;
                case ItemKind::Model: models++; break;
                case ItemKind::Light: lights++; break;
                case ItemKind::Spawn: break;
            }
        }

        ImGui::SeparatorText("Level");
        ImGui::Text("%d boxes, %d ramps, %d stairs", boxes, ramps, stairs);
        ImGui::Text("%d wedges, %d cylinders", wedges, cylinders);
        ImGui::Text("%d models, %d lights", models, lights);
        return;
    }

    if (count > 1)
    {
        ImGui::Text("%d items selected", count);
        ImGui::SeparatorText("Visibility");

        float spacing = ImGui::GetStyle().ItemSpacing.x;
        float half = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
        bool hideAll = ImGui::Button("Hide", ImVec2(half, 0.0f));
        ImGui::SameLine();
        bool showAll = ImGui::Button("Show", ImVec2(half, 0.0f));
        bool lockAll = ImGui::Button("Lock", ImVec2(half, 0.0f));
        ImGui::SameLine();
        bool unlockAll = ImGui::Button("Unlock", ImVec2(half, 0.0f));

        if (hideAll || showAll || lockAll || unlockAll)
        {
            for (const ItemInfo& item : m_items)
            {
                if (!item.selected)
                {
                    continue;
                }
                if (hideAll || showAll)
                {
                    m_host.setItemHidden(item.id, hideAll);
                }
                if (lockAll || unlockAll)
                {
                    m_host.setItemLocked(item.id, lockAll);
                }
            }
        }

        ImGui::SeparatorText("Actions");
        drawSelectionActions(false);
        return;
    }

    ItemInfo info;
    if (!m_host.primaryItem(info))
    {
        return;
    }

    ImGui::TextDisabled("%s", kindTag(info.kind));
    ImGui::SameLine();

    if (!m_nameEditing)
    {
        std::snprintf(m_nameBuffer, sizeof(m_nameBuffer), "%s", info.name.c_str());
    }
    if (m_focusName)
    {
        ImGui::SetKeyboardFocusHere();
        m_focusName = false;
    }
    ImGui::SetNextItemWidth(-1.0f);
    bool enter = ImGui::InputText("##name", m_nameBuffer, sizeof(m_nameBuffer),
                                  ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    m_nameEditing = ImGui::IsItemActive();
    if (enter || ImGui::IsItemDeactivatedAfterEdit())
    {
        m_host.renameItem(info.id, m_nameBuffer);
    }

    bool visible = info.visible;
    if (ImGui::Checkbox("Visible", &visible))
    {
        m_host.setItemHidden(info.id, !visible);
    }
    ImGui::SameLine();
    bool locked = info.locked;
    if (ImGui::Checkbox("Locked", &locked))
    {
        m_host.setItemLocked(info.id, locked);
    }
    if (info.locked)
    {
        wrappedHint("Locked items cannot be picked, moved or deleted in the view. Unlock to edit.");
    }

    ImGui::BeginDisabled(info.locked);

    ImGui::SeparatorText("Transform");
    glm::vec3 position = info.position;
    float yaw = info.yawDegrees;
    float scale = info.scale;
    bool moved = ImGui::DragFloat3("Position", &position.x, 0.05f, 0.0f, 0.0f, "%.2f");
    moved |= ImGui::SliderFloat("Yaw", &yaw, -180.0f, 180.0f, "%.0f deg");
    if (info.kind == ItemKind::Model)
    {
        moved |= ImGui::SliderFloat("Scale", &scale, 0.001f, 100.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
    }
    if (moved)
    {
        m_host.setPrimaryTransform(position, yaw, scale);
    }

    if (info.kind == ItemKind::Box || info.kind == ItemKind::Ramp || info.kind == ItemKind::Stairs ||
        info.kind == ItemKind::Wedge || info.kind == ItemKind::Cylinder)
    {
        ImGui::SeparatorText("Brush");
        if (BrushDef* brush = m_host.selectedBrush())
        {
            drawBrushProperties(*brush);
        }
    }
    else if (info.kind == ItemKind::Light)
    {
        ImGui::SeparatorText("Light");
        if (LightDef* light = m_host.selectedLight())
        {
            drawLightProperties(*light);
        }
        ImGui::TextDisabled("Position is the base of the marker");
    }
    else if (info.kind == ItemKind::Spawn)
    {
        ImGui::SeparatorText("Player spawn");
        wrappedHint("Walk mode (P) starts here, facing the green arrow. Position is the player's feet.");
    }
    else
    {
        drawModelProperties(info);
    }

    ImGui::EndDisabled();

    ImGui::SeparatorText("Actions");
    drawSelectionActions(info.locked);
}

void Editor::drawModelProperties(const ItemInfo& info)
{
    ImGui::SeparatorText("Model");
    bool solid = info.solid;
    if (ImGui::Checkbox("Solid in walk mode", &solid))
    {
        m_host.setPrimarySolid(solid);
    }
    hint("Solid models block the player as a box around them");
    ImGui::Text("Size  %.2f x %.2f x %.2f", info.nativeSize.x, info.nativeSize.y, info.nativeSize.z);

    float longest = std::max(info.nativeSize.x, std::max(info.nativeSize.y, info.nativeSize.z));
    ImGui::SetNextItemWidth(100.0f);
    ImGui::DragFloat("##fit", &m_fitSize, 0.1f, 0.1f, 10000.0f, "%.1f");
    hint("Target length of the model's longest side");
    ImGui::SameLine();
    if (ImGui::Button("Fit to size") && longest > 1e-6f && info.scale > 1e-6f)
    {
        float baseLongest = longest / info.scale;
        m_host.setPrimaryTransform(info.position, info.yawDegrees, m_fitSize / baseLongest);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset scale"))
    {
        m_host.setPrimaryTransform(info.position, info.yawDegrees, 1.0f);
    }
}

void Editor::drawSelectionActions(bool locked)
{
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float width = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;

    ImGui::BeginDisabled(locked);
    if (ImGui::Button("Snap to grid", ImVec2(width, 0.0f)))
    {
        m_host.snapSelectedToGrid(m_gridSize);
    }
    ImGui::SameLine();
    if (ImGui::Button("Rotate 90  (R)", ImVec2(width, 0.0f)))
    {
        m_host.rotateSelected(90.0f);
    }
    ImGui::EndDisabled();

    if (ImGui::Button("Duplicate", ImVec2(width, 0.0f)))
    {
        execute(EditorCommand::Duplicate);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(locked);
    if (ImGui::Button("Delete", ImVec2(width, 0.0f)))
    {
        m_host.deleteSelection();
    }
    ImGui::EndDisabled();
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
    else if (brush.shape == BrushShape::Cylinder)
    {
        brush.steps = std::clamp(brush.steps, 3, 48);
        changed |= ImGui::SliderInt("Sides##selected", &brush.steps, 3, 48);
    }
    changed |= ImGui::DragFloat3("Size##selected", &brush.size.x, 0.05f, 0.05f, 1000.0f, "%.2f");
    changed |= ImGui::ColorEdit3("Color##selected", &brush.color.x);
    changed |= ImGui::SliderFloat("Roughness##selected", &brush.roughness, 0.05f, 1.0f, "%.2f");
    changed |= ImGui::SliderFloat("Metallic##selected", &brush.metallic, 0.0f, 1.0f, "%.2f");
    changed |= drawTextureControls(brush, "##selected");

    if (changed)
    {
        m_host.applySelectedBrush();
    }
}

bool Editor::drawTextureControls(BrushDef& brush, const char* suffix)
{
    bool changed = false;

    ImGui::Text("Texture: %s", brush.texture.empty() ? "None (plain color)" : brush.texture.c_str());

    std::vector<std::string> options;
    options.push_back("None");
    options.insert(options.end(), m_textureNames.begin(), m_textureNames.end());

    float available = ImGui::GetContentRegionAvail().x;
    float used = 0.0f;
    const ImGuiStyle& style = ImGui::GetStyle();

    for (size_t i = 0; i < options.size(); i++)
    {
        const std::string& name = options[i];
        bool none = i == 0;
        bool active = none ? brush.texture.empty() : brush.texture == name;

        float width = ImGui::CalcTextSize(name.c_str()).x + style.FramePadding.x * 2.0f;
        if (i > 0)
        {
            if (used + style.ItemSpacing.x + width <= available)
            {
                ImGui::SameLine();
                used += style.ItemSpacing.x;
            }
            else
            {
                used = 0.0f;
            }
        }
        used += width;

        std::string id = name + suffix;
        if (toggleButton(id.c_str(), active))
        {
            brush.texture = none ? std::string() : name;
            changed = true;
        }
    }

    if (!brush.texture.empty())
    {
        std::string tileLabel = std::string("Tile size") + suffix;
        changed |= ImGui::DragFloat(tileLabel.c_str(), &brush.textureScale, 0.05f, 0.1f, 100.0f, "%.2f m");
        hint("World-space length of one texture repeat, so the pattern keeps its scale when a brush is resized");
    }
    return changed;
}

void Editor::drawLightProperties(LightDef& light)
{
    ImGui::Checkbox("Enabled##light", &light.enabled);
    ImGui::ColorEdit3("Color##light", &light.color.x);
    ImGui::DragFloat("Intensity##light", &light.intensity, 0.25f, 0.0f, 1000.0f, "%.1f");
    ImGui::DragFloat("Radius##light", &light.radius, 0.1f, 0.5f, 200.0f, "%.1f m");
    hint("Distance at which the light fades to nothing");
}

void Editor::drawStatusBar(float framesPerSecond)
{
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    if (ImGui::Begin("##statusbar", nullptr, flags))
    {
        char left[256];
        std::snprintf(left, sizeof(left), "%s%s    %s    Grid %.2f    %d selected",
                      m_levelName, m_dirty ? " *" : "", m_tool.armed() ? "Draw" : "Select", m_gridSize,
                      m_host.selectionCount());
        ImGui::TextUnformatted(left);

        if (!m_status.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("    %s", m_status.c_str());
        }

        char right[64];
        std::snprintf(right, sizeof(right), "%.0f FPS", framesPerSecond);
        float rightWidth = ImGui::CalcTextSize(right).x;
        ImGui::SameLine(ImGui::GetWindowWidth() - rightWidth - ImGui::GetStyle().WindowPadding.x);
        ImGui::TextDisabled("%s", right);
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void Editor::drawSaveAsPopup()
{
    if (m_requestSaveAs)
    {
        ImGui::OpenPopup("Save level as");
        m_requestSaveAs = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Save level as", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (ImGui::IsWindowAppearing())
        {
            ImGui::SetKeyboardFocusHere();
        }
        bool enter = ImGui::InputText("Name", m_saveAsName, sizeof(m_saveAsName), ImGuiInputTextFlags_EnterReturnsTrue);

        if (ImGui::Button("Save", ImVec2(120.0f, 0.0f)) || enter)
        {
            if (m_saveAsName[0] != '\0')
            {
                std::snprintf(m_levelName, sizeof(m_levelName), "%s", m_saveAsName);
                saveLevel();
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void Editor::drawUnsavedPopup()
{
    if (m_requestUnsaved)
    {
        ImGui::OpenPopup("Unsaved changes");
        m_requestUnsaved = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Save changes to \"%s\" before continuing?", m_levelName);
        ImGui::Spacing();
        if (ImGui::Button("Save", ImVec2(120.0f, 0.0f)))
        {
            if (saveLevel())
            {
                performPending();
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't save", ImVec2(120.0f, 0.0f)))
        {
            performPending();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
        {
            m_pending = PendingAction::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void Editor::drawRecoverPopup()
{
    if (m_requestRecover)
    {
        ImGui::OpenPopup("Recover level");
        m_requestRecover = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Recover level", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("An autosave of \"%s\" is newer than the saved level.", m_recoverName.c_str());
        ImGui::Text("Recover it?");
        ImGui::Spacing();
        if (ImGui::Button("Recover", ImVec2(120.0f, 0.0f)))
        {
            if (applyLevel(autosavePath(m_recoverName), m_recoverName, true))
            {
                m_status = "Recovered autosave of " + m_recoverName;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard", ImVec2(120.0f, 0.0f)))
        {
            removeAutosave(m_recoverName);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

#pragma once
#include "Brush.h"
#include "BrushHandles.h"
#include "BrushTool.h"
#include "EditorHost.h"
#include "History.h"

#include <string>
#include <vector>

enum class EditorCommand
{
    Undo,
    Redo,
    Duplicate,
    SelectAll,
    DrawTool,
    SelectTool,
    ToggleGrid,
    Rotate,
    Save,
    AddLight,
    Focus,
    Rename,
    Delete
};

class Editor
{
public:
    explicit Editor(EditorHost& host);

    void drawFileMenu();
    void drawEditMenu();
    void drawAddMenu();
    void drawViewMenu();
    void drawPanels(float framesPerSecond);

    void update(bool overlayVisible);
    void drawOverlayUi();
    bool onLeftPress();
    void onLeftRelease();
    bool cancelTool();
    bool execute(EditorCommand command);
    void noteInput() { m_historyDirty = true; }
    bool dirty() const { return m_dirty; }
    const char* levelName() const { return m_levelName; }

private:
    void drawToolsPanel();
    void drawOutlinerPanel();
    void drawInspectorPanel();
    void drawStatusBar(float framesPerSecond);
    void drawSaveAsPopup();
    void drawBrushProperties(BrushDef& brush);
    void drawLightProperties(LightDef& light);
    void drawModelProperties(const ItemInfo& info);
    void drawSelectionActions(bool locked);
    bool drawTextureControls(BrushDef& brush, const char* suffix);

    void addBrush(BrushShape shape);
    void addLight();
    void newLevel();
    void refreshTextureList();
    void toggleDrawTool();
    void trackHistory(bool overlayVisible);
    void flushHistory();
    LevelData captureState() const;
    void restoreState(const LevelData& state);
    bool undo();
    bool redo();

    void createBrushFromBounds(const BrushBounds& bounds);
    void drawGrid();
    void drawSelection();
    void saveLevel();
    void loadLevel(const std::string& name);
    void refreshLevelList();
    std::string levelPath(const std::string& name) const;
    float snapCenter(float value, float size) const;

    EditorHost& m_host;
    BrushDef m_newBrush;
    float m_gridSize = 0.5f;
    BrushTool m_tool;
    BrushHandles m_handles;
    std::string m_label;
    bool m_showGrid = true;
    bool m_showSelection = true;
    bool m_showLightMarkers = true;
    bool m_showTools = true;
    bool m_showOutliner = true;
    bool m_showInspector = true;
    int m_nextBrush = 1;
    int m_nextLight = 1;
    std::vector<std::string> m_textureNames;
    char m_levelName[64] = "level";
    std::vector<std::string> m_levelFiles;
    std::string m_status;
    History m_history;
    bool m_historyDirty = false;
    bool m_dirty = false;

    std::vector<ItemInfo> m_items;
    char m_outlinerFilter[64] = {};
    char m_nameBuffer[64] = {};
    bool m_nameEditing = false;
    bool m_focusName = false;
    bool m_requestSaveAs = false;
    char m_saveAsName[64] = {};
    float m_fitSize = 10.0f;
};

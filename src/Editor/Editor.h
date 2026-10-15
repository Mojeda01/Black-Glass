#pragma once
#include "Brush.h"
#include "BrushHandles.h"
#include "BrushTool.h"
#include "EditorHost.h"

#include <string>
#include <vector>

class Editor
{
public:
    explicit Editor(EditorHost& host);

    void drawPanel();
    void update(bool overlayVisible);
    void drawOverlayUi();
    bool onLeftPress();
    void onLeftRelease();
    bool cancelTool();

private:
    void drawBrushSection();
    void drawSelectedBrushSection();
    void drawLevelSection();

    void addBrush();
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
    int m_nextBrush = 1;
    char m_levelName[64] = "level";
    std::vector<std::string> m_levelFiles;
    std::string m_status;
};

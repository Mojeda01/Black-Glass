#pragma once
#include "Brush.h"
#include "EditorHost.h"

#include <string>
#include <vector>

class Editor
{
public:
    explicit Editor(EditorHost& host);

    void drawPanel();

private:
    void drawBrushSection();
    void drawSelectedBrushSection();
    void drawLevelSection();

    void addBrush();
    void saveLevel();
    void loadLevel(const std::string& name);
    void refreshLevelList();
    std::string levelPath(const std::string& name) const;
    float snapCenter(float value, float size) const;

    EditorHost& m_host;
    BrushDef m_newBrush;
    float m_gridSize = 0.5f;
    int m_nextBrush = 1;
    char m_levelName[64] = "level";
    std::vector<std::string> m_levelFiles;
    std::string m_status;
};

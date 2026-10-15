#pragma once
#include "Level.h"

#include <string>

bool saveLevelFile(const std::string& path, const LevelData& level, std::string& error);
bool loadLevelFile(const std::string& path, LevelData& level, std::string& error);

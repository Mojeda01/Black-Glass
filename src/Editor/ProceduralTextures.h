#pragma once
#include "../MeshData.h"

#include <string>
#include <vector>

const std::vector<std::string>& proceduralTextureNames();
bool generateProceduralTexture(const std::string& name, ImageData& out);

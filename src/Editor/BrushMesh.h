#pragma once
#include "Brush.h"
#include "../Debug/DebugDraw.h"
#include "../MeshData.h"

MeshData buildBrushMesh(const BrushDef& def, const ImageData* texture = nullptr);
MeshData buildLightMarkerMesh();

void drawBrushOutline(const BrushDef& def, const glm::mat4& unitTransform, DebugDraw& debug,
                      const glm::vec4& color, bool overlay);

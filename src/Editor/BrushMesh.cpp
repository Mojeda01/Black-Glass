#include "BrushMesh.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    const int MAX_STEPS = 64;

    void addPolygon(MeshData& mesh, const std::vector<glm::vec3>& points, const glm::vec3& normal)
    {
        glm::vec3 tangent = glm::normalize(points[1] - points[0]);
        glm::vec3 bitangent = glm::cross(normal, tangent);

        uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        for (const glm::vec3& point : points)
        {
            Vertex v{};
            v.pos = point;
            v.color = glm::vec3(1.0f);
            v.texCoord = glm::vec2(glm::dot(point - points[0], tangent), -glm::dot(point - points[0], bitangent));
            v.normal = normal;
            v.tangent = glm::vec4(tangent, 1.0f);
            mesh.vertices.push_back(v);
        }

        for (uint32_t i = 1; i + 1 < points.size(); i++)
        {
            glm::vec3 geometric = glm::cross(points[i] - points[0], points[i + 1] - points[0]);
            if (glm::dot(geometric, normal) >= 0.0f)
            {
                mesh.indices.push_back(base);
                mesh.indices.push_back(base + i);
                mesh.indices.push_back(base + i + 1);
            }
            else
            {
                mesh.indices.push_back(base);
                mesh.indices.push_back(base + i + 1);
                mesh.indices.push_back(base + i);
            }
        }
    }

    void buildBox(MeshData& mesh)
    {
        addPolygon(mesh, { { 0.5f, 0.0f, -0.5f }, { 0.5f, 0.0f, 0.5f }, { 0.5f, 1.0f, 0.5f }, { 0.5f, 1.0f, -0.5f } }, { 1.0f, 0.0f, 0.0f });
        addPolygon(mesh, { { -0.5f, 0.0f, -0.5f }, { -0.5f, 0.0f, 0.5f }, { -0.5f, 1.0f, 0.5f }, { -0.5f, 1.0f, -0.5f } }, { -1.0f, 0.0f, 0.0f });
        addPolygon(mesh, { { -0.5f, 1.0f, -0.5f }, { 0.5f, 1.0f, -0.5f }, { 0.5f, 1.0f, 0.5f }, { -0.5f, 1.0f, 0.5f } }, { 0.0f, 1.0f, 0.0f });
        addPolygon(mesh, { { -0.5f, 0.0f, -0.5f }, { 0.5f, 0.0f, -0.5f }, { 0.5f, 0.0f, 0.5f }, { -0.5f, 0.0f, 0.5f } }, { 0.0f, -1.0f, 0.0f });
        addPolygon(mesh, { { -0.5f, 0.0f, 0.5f }, { 0.5f, 0.0f, 0.5f }, { 0.5f, 1.0f, 0.5f }, { -0.5f, 1.0f, 0.5f } }, { 0.0f, 0.0f, 1.0f });
        addPolygon(mesh, { { -0.5f, 0.0f, -0.5f }, { 0.5f, 0.0f, -0.5f }, { 0.5f, 1.0f, -0.5f }, { -0.5f, 1.0f, -0.5f } }, { 0.0f, 0.0f, -1.0f });
    }

    void buildRamp(MeshData& mesh)
    {
        addPolygon(mesh, { { -0.5f, 0.0f, -0.5f }, { 0.5f, 0.0f, -0.5f }, { 0.5f, 1.0f, 0.5f }, { -0.5f, 1.0f, 0.5f } },
                   glm::normalize(glm::vec3(0.0f, 1.0f, -1.0f)));
        addPolygon(mesh, { { -0.5f, 0.0f, 0.5f }, { 0.5f, 0.0f, 0.5f }, { 0.5f, 1.0f, 0.5f }, { -0.5f, 1.0f, 0.5f } }, { 0.0f, 0.0f, 1.0f });
        addPolygon(mesh, { { -0.5f, 0.0f, -0.5f }, { 0.5f, 0.0f, -0.5f }, { 0.5f, 0.0f, 0.5f }, { -0.5f, 0.0f, 0.5f } }, { 0.0f, -1.0f, 0.0f });
        addPolygon(mesh, { { -0.5f, 0.0f, -0.5f }, { -0.5f, 0.0f, 0.5f }, { -0.5f, 1.0f, 0.5f } }, { -1.0f, 0.0f, 0.0f });
        addPolygon(mesh, { { 0.5f, 0.0f, -0.5f }, { 0.5f, 0.0f, 0.5f }, { 0.5f, 1.0f, 0.5f } }, { 1.0f, 0.0f, 0.0f });
    }

    void buildStairs(MeshData& mesh, int steps)
    {
        const float n = static_cast<float>(steps);
        for (int i = 0; i < steps; i++)
        {
            float z0 = -0.5f + i / n;
            float z1 = -0.5f + (i + 1) / n;
            float y0 = i / n;
            float y1 = (i + 1) / n;

            addPolygon(mesh, { { -0.5f, y1, z0 }, { 0.5f, y1, z0 }, { 0.5f, y1, z1 }, { -0.5f, y1, z1 } }, { 0.0f, 1.0f, 0.0f });
            addPolygon(mesh, { { -0.5f, y0, z0 }, { 0.5f, y0, z0 }, { 0.5f, y1, z0 }, { -0.5f, y1, z0 } }, { 0.0f, 0.0f, -1.0f });
            addPolygon(mesh, { { -0.5f, 0.0f, z0 }, { -0.5f, 0.0f, z1 }, { -0.5f, y1, z1 }, { -0.5f, y1, z0 } }, { -1.0f, 0.0f, 0.0f });
            addPolygon(mesh, { { 0.5f, 0.0f, z0 }, { 0.5f, 0.0f, z1 }, { 0.5f, y1, z1 }, { 0.5f, y1, z0 } }, { 1.0f, 0.0f, 0.0f });
        }

        addPolygon(mesh, { { -0.5f, 0.0f, 0.5f }, { 0.5f, 0.0f, 0.5f }, { 0.5f, 1.0f, 0.5f }, { -0.5f, 1.0f, 0.5f } }, { 0.0f, 0.0f, 1.0f });
        addPolygon(mesh, { { -0.5f, 0.0f, -0.5f }, { 0.5f, 0.0f, -0.5f }, { 0.5f, 0.0f, 0.5f }, { -0.5f, 0.0f, 0.5f } }, { 0.0f, -1.0f, 0.0f });
    }
}

MeshData buildBrushMesh(const BrushDef& def)
{
    MeshData mesh;

    switch (def.shape)
    {
        case BrushShape::Box:
            buildBox(mesh);
            break;
        case BrushShape::Ramp:
            buildRamp(mesh);
            break;
        case BrushShape::Stairs:
            buildStairs(mesh, std::clamp(def.steps, 1, MAX_STEPS));
            break;
    }

    Material material;
    material.params.baseColorFactor = glm::vec4(def.color, 1.0f);
    material.params.factors = glm::vec4(def.metallic, def.roughness, 1.0f, 1.0f);
    mesh.materials.push_back(material);

    Primitive primitive{};
    primitive.firstIndex = 0;
    primitive.indexCount = static_cast<uint32_t>(mesh.indices.size());
    primitive.materialIndex = 0;
    mesh.primitives.push_back(primitive);

    return mesh;
}

void drawBrushOutline(const BrushDef& def, const glm::mat4& unitTransform, DebugDraw& debug,
                      const glm::vec4& color, bool overlay)
{
    auto point = [&](float x, float y, float z)
    {
        return glm::vec3(unitTransform * glm::vec4(x, y, z, 1.0f));
    };

    switch (def.shape)
    {
        case BrushShape::Box:
        {
            debug.obb(unitTransform, glm::vec3(-0.5f, 0.0f, -0.5f), glm::vec3(0.5f, 1.0f, 0.5f), color, overlay);
            break;
        }
        case BrushShape::Ramp:
        {
            glm::vec3 b00 = point(-0.5f, 0.0f, -0.5f);
            glm::vec3 b10 = point(0.5f, 0.0f, -0.5f);
            glm::vec3 b11 = point(0.5f, 0.0f, 0.5f);
            glm::vec3 b01 = point(-0.5f, 0.0f, 0.5f);
            glm::vec3 t0 = point(-0.5f, 1.0f, 0.5f);
            glm::vec3 t1 = point(0.5f, 1.0f, 0.5f);

            debug.line(b00, b10, color, overlay);
            debug.line(b10, b11, color, overlay);
            debug.line(b11, b01, color, overlay);
            debug.line(b01, b00, color, overlay);
            debug.line(t0, t1, color, overlay);
            debug.line(b01, t0, color, overlay);
            debug.line(b11, t1, color, overlay);
            debug.line(b00, t0, color, overlay);
            debug.line(b10, t1, color, overlay);
            break;
        }
        case BrushShape::Stairs:
        {
            const int steps = std::clamp(def.steps, 1, MAX_STEPS);
            const float n = static_cast<float>(steps);

            for (int side = 0; side < 2; side++)
            {
                float x = side == 0 ? -0.5f : 0.5f;
                glm::vec3 previous = point(x, 0.0f, -0.5f);
                for (int i = 0; i < steps; i++)
                {
                    float z0 = -0.5f + i / n;
                    float z1 = -0.5f + (i + 1) / n;
                    float y0 = i / n;
                    float y1 = (i + 1) / n;

                    glm::vec3 riserBottom = point(x, y0, z0);
                    glm::vec3 riserTop = point(x, y1, z0);
                    glm::vec3 treadEnd = point(x, y1, z1);

                    debug.line(previous, riserBottom, color, overlay);
                    debug.line(riserBottom, riserTop, color, overlay);
                    debug.line(riserTop, treadEnd, color, overlay);
                    previous = treadEnd;
                }
                glm::vec3 backBottom = point(x, 0.0f, 0.5f);
                debug.line(previous, backBottom, color, overlay);
                debug.line(backBottom, point(x, 0.0f, -0.5f), color, overlay);
            }

            for (int i = 0; i < steps; i++)
            {
                float z0 = -0.5f + i / n;
                float z1 = -0.5f + (i + 1) / n;
                float y1 = (i + 1) / n;
                debug.line(point(-0.5f, y1, z0), point(0.5f, y1, z0), color, overlay);
                debug.line(point(-0.5f, y1, z1), point(0.5f, y1, z1), color, overlay);
            }
            debug.line(point(-0.5f, 0.0f, -0.5f), point(0.5f, 0.0f, -0.5f), color, overlay);
            debug.line(point(-0.5f, 0.0f, 0.5f), point(0.5f, 0.0f, 0.5f), color, overlay);
            break;
        }
    }
}

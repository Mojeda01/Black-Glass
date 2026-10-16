#include "BrushMesh.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    const int MAX_STEPS = 64;
    const int MIN_SIDES = 3;
    const int MAX_SIDES = 48;
    const float TWO_PI = 6.28318530717959f;

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

    void addSmoothQuad(MeshData& mesh, const glm::vec3 (&points)[4], const glm::vec3 (&normals)[4], float width)
    {
        glm::vec3 tangent = glm::normalize(points[1] - points[0]);
        glm::vec3 faceNormal = glm::normalize(normals[0] + normals[1]);

        uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        const float u[4] = { 0.0f, width, width, 0.0f };
        for (int i = 0; i < 4; i++)
        {
            Vertex v{};
            v.pos = points[i];
            v.color = glm::vec3(1.0f);
            v.texCoord = glm::vec2(u[i], -points[i].y);
            v.normal = normals[i];
            v.tangent = glm::vec4(tangent, 1.0f);
            mesh.vertices.push_back(v);
        }

        glm::vec3 geometric = glm::cross(points[1] - points[0], points[2] - points[0]);
        bool forward = glm::dot(geometric, faceNormal) >= 0.0f;
        const uint32_t order[6] = { 0, 1, 2, 0, 2, 3 };
        const uint32_t reversed[6] = { 0, 2, 1, 0, 3, 2 };
        for (int i = 0; i < 6; i++)
        {
            mesh.indices.push_back(base + (forward ? order[i] : reversed[i]));
        }
    }

    void buildWedge(MeshData& mesh)
    {
        const glm::vec3 a0(-0.5f, 0.0f, -0.5f);
        const glm::vec3 b0(0.5f, 0.0f, -0.5f);
        const glm::vec3 c0(-0.5f, 0.0f, 0.5f);
        const glm::vec3 a1(-0.5f, 1.0f, -0.5f);
        const glm::vec3 b1(0.5f, 1.0f, -0.5f);
        const glm::vec3 c1(-0.5f, 1.0f, 0.5f);

        addPolygon(mesh, { a0, b0, c0 }, { 0.0f, -1.0f, 0.0f });
        addPolygon(mesh, { a1, b1, c1 }, { 0.0f, 1.0f, 0.0f });
        addPolygon(mesh, { a0, b0, b1, a1 }, { 0.0f, 0.0f, -1.0f });
        addPolygon(mesh, { a0, c0, c1, a1 }, { -1.0f, 0.0f, 0.0f });
        addPolygon(mesh, { b0, c0, c1, b1 }, glm::normalize(glm::vec3(1.0f, 0.0f, 1.0f)));
    }

    void buildCylinder(MeshData& mesh, int sides)
    {
        const float n = static_cast<float>(sides);
        std::vector<glm::vec3> bottom;
        std::vector<glm::vec3> top;
        for (int i = 0; i < sides; i++)
        {
            float angle = TWO_PI * static_cast<float>(i) / n;
            float x = std::cos(angle) * 0.5f;
            float z = std::sin(angle) * 0.5f;
            bottom.push_back({ x, 0.0f, z });
            top.push_back({ x, 1.0f, z });
        }

        std::vector<glm::vec3> capTop = top;
        std::reverse(capTop.begin(), capTop.end());
        addPolygon(mesh, capTop, { 0.0f, 1.0f, 0.0f });
        addPolygon(mesh, bottom, { 0.0f, -1.0f, 0.0f });

        const float arc = TWO_PI * 0.5f / n;
        for (int i = 0; i < sides; i++)
        {
            int j = (i + 1) % sides;
            float angleA = TWO_PI * static_cast<float>(i) / n;
            float angleB = TWO_PI * static_cast<float>(i + 1) / n;
            glm::vec3 normalA(std::cos(angleA), 0.0f, std::sin(angleA));
            glm::vec3 normalB(std::cos(angleB), 0.0f, std::sin(angleB));

            const glm::vec3 points[4] = { bottom[i], bottom[j], top[j], top[i] };
            const glm::vec3 normals[4] = { normalA, normalB, normalB, normalA };
            addSmoothQuad(mesh, points, normals, arc * 2.0f);
        }
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

MeshData buildBrushMesh(const BrushDef& def, const ImageData* texture)
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
        case BrushShape::Wedge:
            buildWedge(mesh);
            break;
        case BrushShape::Cylinder:
            buildCylinder(mesh, std::clamp(def.steps, MIN_SIDES, MAX_SIDES));
            break;
    }

    Material material;
    material.params.baseColorFactor = glm::vec4(def.color, 1.0f);
    material.params.factors = glm::vec4(def.metallic, def.roughness, 1.0f, 1.0f);

    bool textured = texture && !texture->pixels.empty() && !def.texture.empty();
    material.params.worldUV = glm::vec4(textured ? 1.0f : 0.0f, def.textureScale, 0.0f, 0.0f);
    if (textured)
    {
        mesh.images.push_back(*texture);
        mesh.samplers.push_back(SamplerData{});
        TextureData data;
        data.image = 0;
        data.sampler = 0;
        data.srgb = true;
        mesh.textures.push_back(data);
        material.textures[0] = 0;
        material.params.flags0.x = 1;
    }
    mesh.materials.push_back(material);

    Primitive primitive{};
    primitive.firstIndex = 0;
    primitive.indexCount = static_cast<uint32_t>(mesh.indices.size());
    primitive.materialIndex = 0;
    mesh.primitives.push_back(primitive);

    return mesh;
}

MeshData buildLightMarkerMesh()
{
    MeshData mesh;

    const float s = 0.2f;
    const glm::vec3 center(0.0f, s, 0.0f);
    for (int sx = -1; sx <= 1; sx += 2)
    {
        for (int sy = -1; sy <= 1; sy += 2)
        {
            for (int sz = -1; sz <= 1; sz += 2)
            {
                glm::vec3 a = center + glm::vec3(static_cast<float>(sx) * s, 0.0f, 0.0f);
                glm::vec3 b = center + glm::vec3(0.0f, static_cast<float>(sy) * s, 0.0f);
                glm::vec3 c = center + glm::vec3(0.0f, 0.0f, static_cast<float>(sz) * s);
                addPolygon(mesh, { a, b, c },
                           glm::normalize(glm::vec3(static_cast<float>(sx), static_cast<float>(sy), static_cast<float>(sz))));
            }
        }
    }

    Material material;
    material.params.baseColorFactor = glm::vec4(0.02f, 0.02f, 0.02f, 1.0f);
    material.params.factors = glm::vec4(0.0f, 1.0f, 1.0f, 1.0f);
    mesh.materials.push_back(material);

    Primitive primitive{};
    primitive.firstIndex = 0;
    primitive.indexCount = static_cast<uint32_t>(mesh.indices.size());
    primitive.materialIndex = 0;
    primitive.castsShadow = false;
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
        case BrushShape::Wedge:
        {
            glm::vec3 a0 = point(-0.5f, 0.0f, -0.5f);
            glm::vec3 b0 = point(0.5f, 0.0f, -0.5f);
            glm::vec3 c0 = point(-0.5f, 0.0f, 0.5f);
            glm::vec3 a1 = point(-0.5f, 1.0f, -0.5f);
            glm::vec3 b1 = point(0.5f, 1.0f, -0.5f);
            glm::vec3 c1 = point(-0.5f, 1.0f, 0.5f);

            debug.line(a0, b0, color, overlay);
            debug.line(b0, c0, color, overlay);
            debug.line(c0, a0, color, overlay);
            debug.line(a1, b1, color, overlay);
            debug.line(b1, c1, color, overlay);
            debug.line(c1, a1, color, overlay);
            debug.line(a0, a1, color, overlay);
            debug.line(b0, b1, color, overlay);
            debug.line(c0, c1, color, overlay);
            break;
        }
        case BrushShape::Cylinder:
        {
            const int sides = std::clamp(def.steps, MIN_SIDES, MAX_SIDES);
            glm::vec3 previousBottom = point(0.5f, 0.0f, 0.0f);
            glm::vec3 previousTop = point(0.5f, 1.0f, 0.0f);
            for (int i = 1; i <= sides; i++)
            {
                float angle = TWO_PI * static_cast<float>(i) / static_cast<float>(sides);
                glm::vec3 nextBottom = point(std::cos(angle) * 0.5f, 0.0f, std::sin(angle) * 0.5f);
                glm::vec3 nextTop = point(std::cos(angle) * 0.5f, 1.0f, std::sin(angle) * 0.5f);
                debug.line(previousBottom, nextBottom, color, overlay);
                debug.line(previousTop, nextTop, color, overlay);
                if (i % std::max(sides / 4, 1) == 0)
                {
                    debug.line(nextBottom, nextTop, color, overlay);
                }
                previousBottom = nextBottom;
                previousTop = nextTop;
            }
            break;
        }
    }
}

#include "BrushMesh.h"

#include <algorithm>
#include <cmath>

namespace
{
    struct Face
    {
        glm::vec3 normal;
        glm::vec3 tangent;
        glm::vec3 bitangent;
    };

    const Face FACES[6] = {
        { { 1.0f, 0.0f, 0.0f },  { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } },
        { { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f },  { 0.0f, 1.0f, 0.0f } },
        { { 0.0f, 1.0f, 0.0f },  { 1.0f, 0.0f, 0.0f },  { 0.0f, 0.0f, -1.0f } },
        { { 0.0f, -1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f },  { 0.0f, 0.0f, 1.0f } },
        { { 0.0f, 0.0f, 1.0f },  { 1.0f, 0.0f, 0.0f },  { 0.0f, 1.0f, 0.0f } },
        { { 0.0f, 0.0f, -1.0f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } }
    };

    float extentAlong(const glm::vec3& axis, const glm::vec3& size)
    {
        return std::abs(axis.x) * size.x + std::abs(axis.y) * size.y + std::abs(axis.z) * size.z;
    }
}

MeshData buildBrushMesh(const BrushDef& def)
{
    glm::vec3 size = glm::max(def.size, glm::vec3(0.01f));
    glm::vec3 center(0.0f, size.y * 0.5f, 0.0f);

    MeshData mesh;
    mesh.vertices.reserve(24);
    mesh.indices.reserve(36);

    for (const Face& face : FACES)
    {
        float tangentExtent = extentAlong(face.tangent, size);
        float bitangentExtent = extentAlong(face.bitangent, size);
        float normalExtent = extentAlong(face.normal, size);

        glm::vec3 faceCenter = center + face.normal * (normalExtent * 0.5f);
        glm::vec3 halfT = face.tangent * (tangentExtent * 0.5f);
        glm::vec3 halfB = face.bitangent * (bitangentExtent * 0.5f);

        const float cornerT[4] = { -1.0f, 1.0f, 1.0f, -1.0f };
        const float cornerB[4] = { -1.0f, -1.0f, 1.0f, 1.0f };

        uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        for (int i = 0; i < 4; i++)
        {
            Vertex v{};
            v.pos = faceCenter + halfT * cornerT[i] + halfB * cornerB[i];
            v.color = glm::vec3(1.0f);
            v.texCoord = glm::vec2((cornerT[i] * 0.5f + 0.5f) * tangentExtent,
                                   (1.0f - (cornerB[i] * 0.5f + 0.5f)) * bitangentExtent);
            v.normal = face.normal;
            v.tangent = glm::vec4(face.tangent, 1.0f);
            mesh.vertices.push_back(v);
        }

        const uint32_t order[6] = { 0, 1, 2, 0, 2, 3 };
        for (uint32_t index : order)
        {
            mesh.indices.push_back(base + index);
        }
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

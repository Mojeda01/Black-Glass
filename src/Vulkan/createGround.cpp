#include "../AlethiaV3.h"
#include <iostream>
#include <algorithm>
#include <limits>

void AlethiaV3::classifyPrimitives(MeshData& mesh)
{
    uint32_t transmissive = 0;
    uint32_t blended = 0;

    for (Primitive& prim : mesh.primitives)
    {
        const MaterialParams& params = mesh.materials[prim.materialIndex].params;

        glm::vec3 lo(std::numeric_limits<float>::max());
        glm::vec3 hi(std::numeric_limits<float>::lowest());
        for (uint32_t i = 0; i < prim.indexCount; i++)
        {
            const glm::vec3& local = mesh.vertices[mesh.indices[prim.firstIndex + i]].pos;
            glm::vec3 world = glm::vec3(prim.worldMatrix * glm::vec4(local, 1.0f));
            lo = glm::min(lo, world);
            hi = glm::max(hi, world);
        }
        prim.boundsCenter = (lo + hi) * 0.5f;

        if (params.transmissionParams.x > 0.0f)
        {
            prim.renderClass = RenderClass::Transmissive;
            prim.castsShadow = false;
            transmissive++;
        }
        else if (params.flags1.y == 2)
        {
            prim.renderClass = RenderClass::Blend;
            prim.castsShadow = false;
            blended++;
        }
        else
        {
            prim.renderClass = RenderClass::Opaque;
        }
    }

    std::cout << "  [classifyPrimitives] " << transmissive << " transmissive, "
              << blended << " blended primitives.\n";
}

void AlethiaV3::addGroundPlane(MeshData& mesh)
{
    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(std::numeric_limits<float>::lowest());

    for (const Primitive& prim : mesh.primitives)
    {
        for (uint32_t i = 0; i < prim.indexCount; i++)
        {
            const glm::vec3& local = mesh.vertices[mesh.indices[prim.firstIndex + i]].pos;
            glm::vec3 world = glm::vec3(prim.worldMatrix * glm::vec4(local, 1.0f));
            lo = glm::min(lo, world);
            hi = glm::max(hi, world);
        }
    }

    M_SCENEMIN = lo;
    M_SCENEMAX = hi;

    glm::vec3 center = (lo + hi) * 0.5f;
    glm::vec3 extent = hi - lo;
    float footprint = std::max(extent.x, extent.z);
    float groundY = lo.y - 0.0005f * std::max(extent.y, 0.01f);
    float radius = 12.0f * footprint;

    M_GROUNDPARAMS = glm::vec4(center.x, center.z, radius, 0.2f * footprint);
    M_GROUNDEXTENT = glm::vec4(extent.x * 0.5f, extent.z * 0.5f, groundY, 0.0f);

    Material ground{};
    ground.params.baseColorFactor = glm::vec4(0.09f, 0.09f, 0.1f, 1.0f);
    ground.params.factors = glm::vec4(0.0f, 0.4f, 1.0f, 1.0f);
    ground.params.flags1 = glm::ivec4(0, 0, 0, 1);
    mesh.materials.push_back(ground);

    uint32_t baseVertex = static_cast<uint32_t>(mesh.vertices.size());
    const float sx[4] = { -1.0f, 1.0f, 1.0f, -1.0f };
    const float sz[4] = { -1.0f, -1.0f, 1.0f, 1.0f };
    for (int i = 0; i < 4; i++)
    {
        Vertex v{};
        v.pos = glm::vec3(center.x + sx[i] * radius, groundY, center.z + sz[i] * radius);
        v.color = glm::vec3(1.0f);
        v.texCoord = glm::vec2(0.0f);
        v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        v.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        mesh.vertices.push_back(v);
    }

    Primitive prim{};
    prim.firstIndex = static_cast<uint32_t>(mesh.indices.size());
    prim.indexCount = 6;
    prim.materialIndex = static_cast<int>(mesh.materials.size()) - 1;
    prim.worldMatrix = glm::mat4(1.0f);
    prim.castsShadow = false;

    const uint32_t quad[6] = { 0, 2, 1, 0, 3, 2 };
    for (uint32_t index : quad)
    {
        mesh.indices.push_back(baseVertex + index);
    }
    mesh.primitives.push_back(prim);

    std::cout << "  [addGroundPlane] SUCCESS - Ground at y=" << groundY
              << ", radius " << radius << ".\n";
}

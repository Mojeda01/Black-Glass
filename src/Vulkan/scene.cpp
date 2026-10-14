#include "../AlethiaV3.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <stdexcept>

#include <glm/gtc/matrix_transform.hpp>

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

void AlethiaV3::createGroundModel()
{
    MeshData mesh;

    const float sx[4] = { -1.0f, 1.0f, 1.0f, -1.0f };
    const float sz[4] = { -1.0f, -1.0f, 1.0f, 1.0f };
    for (int i = 0; i < 4; i++)
    {
        Vertex v{};
        v.pos = glm::vec3(sx[i], 0.0f, sz[i]);
        v.color = glm::vec3(1.0f);
        v.texCoord = glm::vec2(0.0f);
        v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        v.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        mesh.vertices.push_back(v);
    }

    mesh.indices = { 0, 2, 1, 0, 3, 2 };

    Material ground{};
    ground.params.baseColorFactor = glm::vec4(0.09f, 0.09f, 0.1f, 1.0f);
    ground.params.factors = glm::vec4(0.0f, 0.4f, 1.0f, 1.0f);
    ground.params.flags1 = glm::ivec4(0, 0, 0, 1);
    mesh.materials.push_back(ground);

    Primitive prim{};
    prim.firstIndex = 0;
    prim.indexCount = 6;
    prim.materialIndex = 0;
    prim.castsShadow = false;
    mesh.primitives.push_back(prim);

    M_GROUND = std::make_unique<Model>();
    M_GROUND->name = "Ground";
    M_GROUND->isGround = true;
    uploadModelGeometry(*M_GROUND, mesh);
    createModelTextures(*M_GROUND, mesh);

    std::cout << "  [createGroundModel] SUCCESS - Ground quad created.\n";
}

void AlethiaV3::updateModelTransform(Model& model)
{
    glm::vec3 pivot((model.localMin.x + model.localMax.x) * 0.5f,
                    model.localMin.y,
                    (model.localMin.z + model.localMax.z) * 0.5f);

    model.transform = glm::translate(glm::mat4(1.0f), model.position)
                    * glm::rotate(glm::mat4(1.0f), glm::radians(model.yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f))
                    * glm::scale(glm::mat4(1.0f), glm::vec3(model.scale))
                    * glm::translate(glm::mat4(1.0f), -pivot);

    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(std::numeric_limits<float>::lowest());
    for (int corner = 0; corner < 8; corner++)
    {
        glm::vec3 local((corner & 1) ? model.localMax.x : model.localMin.x,
                        (corner & 2) ? model.localMax.y : model.localMin.y,
                        (corner & 4) ? model.localMax.z : model.localMin.z);
        glm::vec3 world = glm::vec3(model.transform * glm::vec4(local, 1.0f));
        lo = glm::min(lo, world);
        hi = glm::max(hi, world);
    }
    model.worldMin = lo;
    model.worldMax = hi;
}

void AlethiaV3::placeNewModel(Model& model)
{
    float sceneMaxX = std::numeric_limits<float>::lowest();
    bool any = false;
    for (const auto& other : M_MODELS)
    {
        if (other->visible)
        {
            sceneMaxX = std::max(sceneMaxX, other->worldMax.x);
            any = true;
        }
    }

    float width = (model.localMax.x - model.localMin.x) * model.scale;
    float x = 0.0f;
    if (any)
    {
        x = sceneMaxX + 0.3f * width + 0.5f * width;
    }
    model.position = glm::vec3(x, 0.0f, 0.0f);
    updateModelTransform(model);
}

void AlethiaV3::refreshScene()
{
    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(std::numeric_limits<float>::lowest());
    float largestFootprint = 0.0f;
    uint32_t boxCount = 0;
    M_GROUNDBOXES.fill(glm::vec4(0.0f));

    for (auto& model : M_MODELS)
    {
        updateModelTransform(*model);
        if (!model->visible)
        {
            continue;
        }

        lo = glm::min(lo, model->worldMin);
        hi = glm::max(hi, model->worldMax);

        glm::vec3 extent = model->worldMax - model->worldMin;
        largestFootprint = std::max(largestFootprint, std::max(extent.x, extent.z));

        if (boxCount < GROUND_BOX_MAX)
        {
            glm::vec3 center = (model->worldMin + model->worldMax) * 0.5f;
            M_GROUNDBOXES[boxCount] = glm::vec4(center.x, center.z, extent.x * 0.5f, extent.z * 0.5f);
            boxCount++;
        }
    }

    if (lo.x > hi.x)
    {
        lo = glm::vec3(-1.0f, 0.0f, -1.0f);
        hi = glm::vec3(1.0f, 1.0f, 1.0f);
        largestFootprint = 2.0f;
    }

    M_SCENEMIN = lo;
    M_SCENEMAX = hi;

    glm::vec3 center = (lo + hi) * 0.5f;
    glm::vec3 extent = hi - lo;
    float footprint = std::max(extent.x, extent.z);
    float groundY = lo.y - 0.0005f * std::max(extent.y, 0.01f);
    float radius = 12.0f * footprint;

    M_GROUNDPARAMS = glm::vec4(center.x, center.z, radius, 0.2f * largestFootprint);
    M_GROUNDEXTENT = glm::vec4(static_cast<float>(boxCount), 0.0f, groundY, 0.0f);

    if (M_GROUND)
    {
        M_GROUND->transform = glm::translate(glm::mat4(1.0f), glm::vec3(center.x, groundY, center.z))
                            * glm::scale(glm::mat4(1.0f), glm::vec3(radius, 1.0f, radius));
    }
}

void AlethiaV3::frameScene()
{
    refreshScene();

    glm::vec3 center = (M_SCENEMIN + M_SCENEMAX) * 0.5f;
    glm::vec3 extent = M_SCENEMAX - M_SCENEMIN;
    float radius = std::max(0.5f * glm::length(extent), 0.1f);

    float aspect = M_SWAPCHAINEXTENT.width / static_cast<float>(std::max(M_SWAPCHAINEXTENT.height, 1u));
    float halfVertical = glm::radians(m_fov) * 0.5f;
    float halfHorizontal = std::atan(std::tan(halfVertical) * aspect);
    float distance = radius / std::sin(std::min(halfVertical, halfHorizontal));

    m_camera.yaw = -90.0f;
    m_camera.pitch = -12.0f;
    m_camera.position = center - m_camera.front() * distance;
}

bool AlethiaV3::loadModel(const std::string& path)
{
    try
    {
        if (!std::filesystem::exists(path))
        {
            throw std::runtime_error("File not found: " + path);
        }

        MeshData mesh = loadGltf(path);
        classifyPrimitives(mesh);

        auto model = std::make_unique<Model>();
        model->path = path;
        model->name = std::filesystem::path(path).stem().string();

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
        model->localMin = lo;
        model->localMax = hi;

        vkDeviceWaitIdle(M_DEVICE);
        try
        {
            uploadModelGeometry(*model, mesh);
            createModelTextures(*model, mesh);
        }
        catch (...)
        {
            destroyModel(*model);
            throw;
        }

        refreshScene();
        placeNewModel(*model);
        M_MODELS.push_back(std::move(model));
        m_shadowDirty = true;
        m_loadError.clear();

        std::cout << "  [loadModel] SUCCESS - " << path << "\n";
        return true;
    }
    catch (const std::exception& e)
    {
        m_loadError = e.what();
        std::cerr << "  [loadModel] FAILED - " << e.what() << "\n";
        return false;
    }
}

void AlethiaV3::destroyModel(Model& model)
{
    destroyBuffer(model.vertexBuffer);
    destroyBuffer(model.indexBuffer);
    destroyBuffer(model.materialBuffer);

    for (auto& image : model.textureImages)
    {
        destroyImage(image);
    }
    model.textureImages.clear();
    model.textures.clear();

    for (VkSampler sampler : model.samplers)
    {
        vkDestroySampler(M_DEVICE, sampler, nullptr);
    }
    model.samplers.clear();
    model.primitives.clear();
    model.materials.clear();
}

void AlethiaV3::removeModel(size_t index)
{
    if (index >= M_MODELS.size())
    {
        return;
    }

    vkDeviceWaitIdle(M_DEVICE);
    destroyModel(*M_MODELS[index]);
    M_MODELS.erase(M_MODELS.begin() + static_cast<std::ptrdiff_t>(index));
    m_shadowDirty = true;
}

#include "../BlackGlass.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <stdexcept>

#include <glm/gtc/matrix_transform.hpp>

void BlackGlass::classifyPrimitives(MeshData& mesh)
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

void BlackGlass::createGroundModel()
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
    M_GROUND->asset = std::make_shared<ModelAsset>();
    M_GROUND->asset->name = "Ground";
    M_GROUND->name = "Ground";
    M_GROUND->isGround = true;
    uploadModelGeometry(*M_GROUND->asset, mesh);
    createModelTextures(*M_GROUND->asset, mesh);

    std::cout << "  [createGroundModel] SUCCESS - Ground quad created.\n";
}

void BlackGlass::updateModelTransform(Model& model)
{
    const glm::vec3& localMin = model.asset->localMin;
    const glm::vec3& localMax = model.asset->localMax;
    glm::vec3 pivot((localMin.x + localMax.x) * 0.5f,
                    localMin.y,
                    (localMin.z + localMax.z) * 0.5f);

    glm::vec3 stretch(model.scale);
    if (model.brush)
    {
        stretch *= model.brush->size;
    }

    model.transform = glm::translate(glm::mat4(1.0f), model.position)
                    * glm::rotate(glm::mat4(1.0f), glm::radians(model.yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f))
                    * glm::scale(glm::mat4(1.0f), stretch)
                    * glm::translate(glm::mat4(1.0f), -pivot);

    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(std::numeric_limits<float>::lowest());
    for (int corner = 0; corner < 8; corner++)
    {
        glm::vec3 local((corner & 1) ? localMax.x : localMin.x,
                        (corner & 2) ? localMax.y : localMin.y,
                        (corner & 4) ? localMax.z : localMin.z);
        glm::vec3 world = glm::vec3(model.transform * glm::vec4(local, 1.0f));
        lo = glm::min(lo, world);
        hi = glm::max(hi, world);
    }
    model.worldMin = lo;
    model.worldMax = hi;
}

void BlackGlass::placeNewModel(Model& model)
{
    float sceneMaxX = std::numeric_limits<float>::lowest();
    bool any = false;
    for (const auto& other : M_MODELS)
    {
        if (other->visible && !other->ghost && other.get() != &model)
        {
            sceneMaxX = std::max(sceneMaxX, other->worldMax.x);
            any = true;
        }
    }

    float width = (model.asset->localMax.x - model.asset->localMin.x) * model.scale;
    if (model.brush)
    {
        width *= model.brush->size.x;
    }
    float x = 0.0f;
    if (any)
    {
        x = sceneMaxX + 0.3f * width + 0.5f * width;
    }
    model.position = glm::vec3(x, 0.0f, 0.0f);
    updateModelTransform(model);
}

void BlackGlass::refreshScene()
{
    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(std::numeric_limits<float>::lowest());
    float largestFootprint = 0.0f;
    uint32_t boxCount = 0;
    M_GROUNDBOXES.fill(glm::vec4(0.0f));

    for (auto& model : M_MODELS)
    {
        updateModelTransform(*model);
        if (!model->visible || model->ghost)
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

void BlackGlass::frameScene()
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

namespace
{
    void computeMeshBounds(const MeshData& mesh, glm::vec3& lo, glm::vec3& hi)
    {
        lo = glm::vec3(std::numeric_limits<float>::max());
        hi = glm::vec3(std::numeric_limits<float>::lowest());
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
    }
}

int BlackGlass::registerAsset(MeshData& mesh, const std::string& name, const std::string& path)
{
    classifyPrimitives(mesh);

    auto asset = std::make_shared<ModelAsset>();
    asset->path = path;
    asset->name = name;
    computeMeshBounds(mesh, asset->localMin, asset->localMax);

    vkDeviceWaitIdle(M_DEVICE);
    try
    {
        uploadModelGeometry(*asset, mesh);
        createModelTextures(*asset, mesh);
    }
    catch (...)
    {
        destroyAsset(*asset);
        throw;
    }

    M_LIBRARY.push_back(std::move(asset));
    return static_cast<int>(M_LIBRARY.size()) - 1;
}

void BlackGlass::replaceAssetMesh(ModelAsset& asset, MeshData& mesh)
{
    classifyPrimitives(mesh);

    vkDeviceWaitIdle(M_DEVICE);
    destroyBuffer(asset.vertexBuffer);
    destroyBuffer(asset.indexBuffer);
    destroyBuffer(asset.materialBuffer);
    asset.materials.clear();
    asset.primitives.clear();

    computeMeshBounds(mesh, asset.localMin, asset.localMax);
    uploadModelGeometry(asset, mesh);
    createModelTextures(asset, mesh);
    m_shadowDirty = true;
}

void BlackGlass::purgeUnusedBrushAssets()
{
    m_brushPurgePending = false;

    for (size_t i = M_LIBRARY.size(); i-- > 0;)
    {
        if (!M_LIBRARY[i]->brush)
        {
            continue;
        }

        bool used = false;
        for (const auto& model : M_MODELS)
        {
            if (model->asset == M_LIBRARY[i])
            {
                used = true;
                break;
            }
        }
        if (!used)
        {
            removeAsset(i);
        }
    }
}

int BlackGlass::importAsset(const std::string& path)
{
    try
    {
        if (!std::filesystem::exists(path))
        {
            throw std::runtime_error("File not found: " + path);
        }

        MeshData mesh = loadGltf(path);
        int index = registerAsset(mesh, std::filesystem::path(path).stem().string(), path);
        m_loadError.clear();

        std::cout << "  [importAsset] SUCCESS - " << path << "\n";
        return index;
    }
    catch (const std::exception& e)
    {
        m_loadError = e.what();
        std::cerr << "  [importAsset] FAILED - " << e.what() << "\n";
        return -1;
    }
}

Model* BlackGlass::addInstance(int assetIndex, bool autoPlace)
{
    if (assetIndex < 0 || assetIndex >= static_cast<int>(M_LIBRARY.size()))
    {
        return nullptr;
    }

    auto model = std::make_unique<Model>();
    model->asset = M_LIBRARY[assetIndex];

    int count = 0;
    for (const auto& other : M_MODELS)
    {
        if (other->asset == model->asset && !other->ghost)
        {
            count++;
        }
    }
    model->name = model->asset->name + (count > 0 ? " #" + std::to_string(count + 1) : "");

    if (autoPlace)
    {
        refreshScene();
        placeNewModel(*model);
        m_shadowDirty = true;
    }
    else
    {
        updateModelTransform(*model);
    }

    M_MODELS.push_back(std::move(model));
    return M_MODELS.back().get();
}

void BlackGlass::destroyAsset(ModelAsset& asset)
{
    destroyBuffer(asset.vertexBuffer);
    destroyBuffer(asset.indexBuffer);
    destroyBuffer(asset.materialBuffer);

    for (auto& image : asset.textureImages)
    {
        destroyImage(image);
    }
    asset.textureImages.clear();
    asset.textures.clear();

    for (VkSampler sampler : asset.samplers)
    {
        vkDestroySampler(M_DEVICE, sampler, nullptr);
    }
    asset.samplers.clear();
    asset.primitives.clear();
    asset.materials.clear();
}

void BlackGlass::removeModel(size_t index)
{
    if (index >= M_MODELS.size())
    {
        return;
    }

    Model* target = M_MODELS[index].get();
    if (m_selected == target)
    {
        m_selected = nullptr;
        m_dragging = false;
    }
    if (m_ghost == target)
    {
        m_ghost = nullptr;
        m_placingAsset = -1;
    }
    bool brushModel = target->asset && target->asset->brush;
    M_MODELS.erase(M_MODELS.begin() + static_cast<std::ptrdiff_t>(index));
    if (brushModel)
    {
        m_brushPurgePending = true;
    }
    m_shadowDirty = true;
}

void BlackGlass::removeAsset(size_t index)
{
    if (index >= M_LIBRARY.size())
    {
        return;
    }

    vkDeviceWaitIdle(M_DEVICE);

    std::shared_ptr<ModelAsset> asset = M_LIBRARY[index];
    for (size_t i = M_MODELS.size(); i-- > 0;)
    {
        if (M_MODELS[i]->asset == asset)
        {
            removeModel(i);
        }
    }

    if (m_placingAsset == static_cast<int>(index))
    {
        cancelPlacement();
    }
    else if (m_placingAsset > static_cast<int>(index))
    {
        m_placingAsset--;
    }

    destroyAsset(*asset);
    M_LIBRARY.erase(M_LIBRARY.begin() + static_cast<std::ptrdiff_t>(index));
    m_shadowDirty = true;
}

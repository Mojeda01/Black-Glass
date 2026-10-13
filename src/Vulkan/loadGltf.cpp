#include "../AlethiaV3.h"
#include <iostream>
#include <stdexcept>
#include <functional>
#include <map>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define TINYGLTF_NO_STB_IMAGE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_IMPLEMENTATION
#include "../tiny_gltf.h"

namespace 
{
    const unsigned char* accessorElement(   const tinygltf::Model& model,
                                            const tinygltf::Accessor& accessor,
                                            size_t i)
    {
        const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
        const tinygltf::Buffer& buffer = model.buffers[view.buffer];

        size_t stride = accessor.ByteStride(view);
        if (stride == 0)
        {
            throw std::runtime_error("Invalid acccessor byte stride!");
        }

        return buffer.data.data() + view.byteOffset + accessor.byteOffset + i * stride;
    }

    glm::mat4 nodeLocalMatrix(const tinygltf::Node& node)
    {
        if (node.matrix.size() == 16)
        {
            glm::mat4 m(1.0f);
            for (int i = 0; i < 16; i++)
            {
                glm::value_ptr(m)[i] = static_cast<float>(node.matrix[i]);
            }
            return m;
        }

        glm::vec3 t(0.0f);
        glm::quat r(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 sc(1.0f);

        if (node.translation.size() == 3)
        {
            t = glm::vec3(node.translation[0], node.translation[1], node.translation[2]);
        }
        if (node.rotation.size() == 4)
        {
            r = glm::quat(static_cast<float>(node.rotation[3]),
                          static_cast<float>(node.rotation[0]),
                          static_cast<float>(node.rotation[1]),
                          static_cast<float>(node.rotation[2]));
        }
        if (node.scale.size() == 3)
        {
            sc = glm::vec3(node.scale[0], node.scale[1], node.scale[2]);
        }

        return glm::translate(glm::mat4(1.0f), t) * glm::mat4_cast(r) * glm::scale(glm::mat4(1.0f), sc);
    }
}   // namespace
 
MeshData AlethiaV3::loadGltf(const std::string& path)
{
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    bool ok = loader.LoadBinaryFromFile(&model, &err, &warn, path);

    if (!warn.empty()) std::cout << "  [loadGltf] WARN: " << warn << "\n";
    if (!ok) throw std::runtime_error("Failed to load glTF: " + err);

    if (model.meshes.empty() || model.nodes.empty())
    {
        throw std::runtime_error("glTF contains no meshes or nodes");
    }

    MeshData mesh;
    std::map<int, std::vector<Primitive>> meshCache;

    auto loadMesh = [&](int meshIndex) -> const std::vector<Primitive>&
    {
        auto cached = meshCache.find(meshIndex);
        if (cached != meshCache.end()) return cached->second;

        std::vector<Primitive>& records = meshCache[meshIndex];

        for (const tinygltf::Primitive& prim : model.meshes[meshIndex].primitives)
        {
            auto posIt = prim.attributes.find("POSITION");
            if (posIt == prim.attributes.end()) continue;
            if (prim.indices < 0) continue;

            uint32_t vertexOffset = static_cast<uint32_t>(mesh.vertices.size());

            Primitive record{};
            record.firstIndex = static_cast<uint32_t>(mesh.indices.size());
            record.materialIndex = prim.material;

            const tinygltf::Accessor& posAccessor = model.accessors[posIt->second];

            const tinygltf::Accessor* uvAccessor = nullptr;
            auto uvIt = prim.attributes.find("TEXCOORD_0");
            if (uvIt != prim.attributes.end())
            {
                uvAccessor = &model.accessors[uvIt->second];
            }

            for (size_t i = 0; i < posAccessor.count; i++)
            {
                Vertex v{};

                const float* pos = reinterpret_cast<const float*>(
                        accessorElement(model, posAccessor, i));
                v.pos = { pos[0], pos[1], pos[2] };
                v.color = { 1.0f, 1.0f, 1.0f };

                if (uvAccessor)
                {
                    const float* uv = reinterpret_cast<const float*>(
                            accessorElement(model, *uvAccessor, i));
                    v.texCoord = { uv[0], uv[1] };
                }

                mesh.vertices.push_back(v);
            }

            const tinygltf::Accessor& idxAccessor = model.accessors[prim.indices];

            for (size_t i = 0; i < idxAccessor.count; i++)
            {
                const unsigned char* raw = accessorElement(model, idxAccessor, i);
                uint32_t index = 0;

                switch (idxAccessor.componentType)
                {
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                        index = *raw; break;
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                        index = *reinterpret_cast<const uint16_t*>(raw); break;
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                        index = *reinterpret_cast<const uint32_t*>(raw); break;
                    default:
                        throw std::runtime_error("Unsupported index component type!");
                }
                mesh.indices.push_back(index + vertexOffset);
            }

            record.indexCount = static_cast<uint32_t>(mesh.indices.size()) - record.firstIndex;
            records.push_back(record);
        }

        return records;
    };

    std::function<void(int, const glm::mat4&)> visitNode = [&](int nodeIndex, const glm::mat4& parent)
    {
        const tinygltf::Node& node = model.nodes[nodeIndex];
        glm::mat4 world = parent * nodeLocalMatrix(node);

        if (node.mesh >= 0)
        {
            for (Primitive instance : loadMesh(node.mesh))
            {
                instance.worldMatrix = world;
                mesh.primitives.push_back(instance);
            }
        }

        for (int child : node.children)
        {
            visitNode(child, world);
        }
    };

    int sceneIndex = model.defaultScene >= 0 ? model.defaultScene : 0;
    if (model.scenes.empty())
    {
        for (size_t i = 0; i < model.nodes.size(); i++)
        {
            visitNode(static_cast<int>(i), glm::mat4(1.0f));
        }
    }
    else
    {
        for (int root : model.scenes[sceneIndex].nodes)
        {
            visitNode(root, glm::mat4(1.0f));
        }
    }

    if (mesh.primitives.empty())
    {
        throw std::runtime_error("glTF mesh has no indexed primitives with POSITION!");
    }

    mesh.images.reserve(model.images.size());
    for (const tinygltf::Image& img : model.images)
    {
        ImageData data;
        if (img.component == 4 && img.bits == 8)
        {
            data.pixels = img.image;
            data.width = img.width;
            data.height = img.height;
        }
        else
        {
            std::cout << "  [loadGltf] WARN: image has " << img.component
                        << " components at " << img.bits << " bits, expected 4 at 8.\n";
        }
        mesh.images.push_back(std::move(data));
    }

    for (Primitive& prim : mesh.primitives)
    {
        if (prim.materialIndex < 0) continue;

        const tinygltf::Material& mat = model.materials[prim.materialIndex];
        int texIndex = mat.pbrMetallicRoughness.baseColorTexture.index;
        if (texIndex < 0) continue;

        int source = model.textures[texIndex].source;
        if (source >= 0 && !mesh.images[source].pixels.empty())
        {
            prim.imageIndex = source;
        }
    }

    std::cout << "  [loadGltf] SUCCESS - " << mesh.vertices.size() << " vertices, "
                << mesh.indices.size() << " indices, "
                << mesh.images.size() << " images, "
                << mesh.primitives.size() << " primitives.\n";
    return mesh;
}

#include "../AlethiaV3.h"
#include <iostream>
#include <stdexcept>

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
}   // namespace
 
MeshData AlethiaV3::loadGltf(const std::string& path)
{
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    bool ok = loader.LoadBinaryFromFile(&model, &err, &warn, path);

    if (!warn.empty()) std::cout << "  [loadGltf] WARN: " << warn << "\n";
    if (!ok) throw std::runtime_error("Failed to load glTF: " + err);

    if (model.meshes.empty() || model.meshes[0].primitives.empty())
    {
        throw std::runtime_error("glTF contains no mesh primitives");
    }

    MeshData mesh;

    for (const tinygltf::Primitive& prim : model.meshes[0].primitives)
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

            const float* pos = reinterpret_cast<const float*> (
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
        mesh.primitives.push_back(record);
    }

    if (mesh.primitives.empty())
    {
        throw std::runtime_error("glTF mesh has no indexed primitives with POSITION!");
    }

    int firstMaterial = mesh.primitives.empty() ? -1 : mesh.primitives[0].materialIndex;
    if (firstMaterial >= 0) 
    {
        const tinygltf::Material& mat = model.materials[firstMaterial];
        int texIndex = mat.pbrMetallicRoughness.baseColorTexture.index;

        if (texIndex >= 0)
        {
            const tinygltf::Texture& tex = model.textures[texIndex];
            const tinygltf::Image& img = model.images[tex.source];

            mesh.texturePixels = img.image;
            mesh.textureWidth = img.width;
            mesh.textureHeight = img.height;
            mesh.hasTexture = (img.component == 4);

            if (img.component != 4)
            {
                std::cout << "  [loadGltf] WARN: image has " << img.component 
                            << " components, expected 4.\n";
            }
        }
    }

    std::cout << "  [loadGltf] SUCCESS - " << mesh.vertices.size() << " vertices, "
                << mesh.indices.size() << " indices, texture "
                << mesh.textureWidth << "x" << mesh.textureHeight << ", "
                << mesh.primitives.size() << " primitives.\n";
    return mesh;
}

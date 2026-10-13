#include "../AlethiaV3.h"
#include <iostream>
#include <stdexcept>
#include <functional>
#include <map>
#include <tuple>
#include <cstring>
#include <cmath>
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
    float readComponent(const unsigned char* p, int componentType, bool normalized)
    {
        switch (componentType)
        {
            case TINYGLTF_COMPONENT_TYPE_FLOAT:
            {
                float v;
                std::memcpy(&v, p, sizeof(v));
                return v;
            }
            case TINYGLTF_COMPONENT_TYPE_BYTE:
            {
                int8_t v;
                std::memcpy(&v, p, sizeof(v));
                return normalized ? std::fmax(v / 127.0f, -1.0f) : static_cast<float>(v);
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            {
                uint8_t v = *p;
                return normalized ? v / 255.0f : static_cast<float>(v);
            }
            case TINYGLTF_COMPONENT_TYPE_SHORT:
            {
                int16_t v;
                std::memcpy(&v, p, sizeof(v));
                return normalized ? std::fmax(v / 32767.0f, -1.0f) : static_cast<float>(v);
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            {
                uint16_t v;
                std::memcpy(&v, p, sizeof(v));
                return normalized ? v / 65535.0f : static_cast<float>(v);
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            {
                uint32_t v;
                std::memcpy(&v, p, sizeof(v));
                return static_cast<float>(v);
            }
            default:
                throw std::runtime_error("Unsupported accessor component type!");
        }
    }

    uint32_t readUnsigned(const unsigned char* p, int componentType)
    {
        switch (componentType)
        {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                return *p;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            {
                uint16_t v;
                std::memcpy(&v, p, sizeof(v));
                return v;
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            {
                uint32_t v;
                std::memcpy(&v, p, sizeof(v));
                return v;
            }
            default:
                throw std::runtime_error("Unsupported index component type!");
        }
    }

    const unsigned char* viewData(const tinygltf::Model& model, int bufferViewIndex, size_t byteOffset)
    {
        if (bufferViewIndex < 0 || bufferViewIndex >= static_cast<int>(model.bufferViews.size()))
        {
            throw std::runtime_error("Invalid buffer view reference!");
        }
        const tinygltf::BufferView& view = model.bufferViews[bufferViewIndex];
        const tinygltf::Buffer& buffer = model.buffers[view.buffer];
        return buffer.data.data() + view.byteOffset + byteOffset;
    }

    std::vector<float> readAccessor(const tinygltf::Model& model, const tinygltf::Accessor& accessor)
    {
        int comps = tinygltf::GetNumComponentsInType(accessor.type);
        int compSize = tinygltf::GetComponentSizeInBytes(accessor.componentType);
        if (comps <= 0 || compSize <= 0)
        {
            throw std::runtime_error("Invalid accessor type!");
        }

        std::vector<float> out(accessor.count * comps, 0.0f);

        if (accessor.bufferView >= 0)
        {
            const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
            int stride = accessor.ByteStride(view);
            if (stride <= 0)
            {
                throw std::runtime_error("Invalid accessor byte stride!");
            }

            const unsigned char* base = viewData(model, accessor.bufferView, accessor.byteOffset);
            for (size_t i = 0; i < accessor.count; i++)
            {
                for (int c = 0; c < comps; c++)
                {
                    out[i * comps + c] = readComponent(base + i * stride + c * compSize,
                                                        accessor.componentType, accessor.normalized);
                }
            }
        }

        if (accessor.sparse.isSparse)
        {
            const auto& sparse = accessor.sparse;
            int indexSize = tinygltf::GetComponentSizeInBytes(sparse.indices.componentType);
            const unsigned char* indexBase = viewData(model, sparse.indices.bufferView,
                                                        sparse.indices.byteOffset);
            const unsigned char* valueBase = viewData(model, sparse.values.bufferView,
                                                        sparse.values.byteOffset);

            for (int k = 0; k < sparse.count; k++)
            {
                uint32_t target = readUnsigned(indexBase + k * indexSize, sparse.indices.componentType);
                if (target >= accessor.count)
                {
                    throw std::runtime_error("Sparse accessor index out of range!");
                }
                for (int c = 0; c < comps; c++)
                {
                    out[target * comps + c] = readComponent(valueBase + (k * comps + c) * compSize,
                                                             accessor.componentType, accessor.normalized);
                }
            }
        }

        return out;
    }

    std::vector<uint32_t> readIndices(const tinygltf::Model& model, const tinygltf::Accessor& accessor)
    {
        if (accessor.bufferView < 0)
        {
            return std::vector<uint32_t>(accessor.count, 0);
        }

        const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
        int stride = accessor.ByteStride(view);
        if (stride <= 0)
        {
            throw std::runtime_error("Invalid index byte stride!");
        }

        const unsigned char* base = viewData(model, accessor.bufferView, accessor.byteOffset);
        std::vector<uint32_t> out(accessor.count);
        for (size_t i = 0; i < accessor.count; i++)
        {
            out[i] = readUnsigned(base + i * stride, accessor.componentType);
        }
        return out;
    }

    void generateNormals(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
    {
        for (Vertex& v : vertices) v.normal = glm::vec3(0.0f);

        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            Vertex& a = vertices[indices[i]];
            Vertex& b = vertices[indices[i + 1]];
            Vertex& c = vertices[indices[i + 2]];
            glm::vec3 n = glm::cross(b.pos - a.pos, c.pos - a.pos);
            a.normal += n;
            b.normal += n;
            c.normal += n;
        }

        for (Vertex& v : vertices)
        {
            float len = glm::length(v.normal);
            v.normal = len > 1e-8f ? v.normal / len : glm::vec3(0.0f, 0.0f, 1.0f);
        }
    }

    void generateTangents(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
    {
        std::vector<glm::vec3> tan(vertices.size(), glm::vec3(0.0f));
        std::vector<glm::vec3> bitan(vertices.size(), glm::vec3(0.0f));

        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            const Vertex& a = vertices[indices[i]];
            const Vertex& b = vertices[indices[i + 1]];
            const Vertex& c = vertices[indices[i + 2]];

            glm::vec3 e1 = b.pos - a.pos;
            glm::vec3 e2 = c.pos - a.pos;
            glm::vec2 d1 = b.texCoord - a.texCoord;
            glm::vec2 d2 = c.texCoord - a.texCoord;
            d1.y = -d1.y;
            d2.y = -d2.y;

            float det = d1.x * d2.y - d2.x * d1.y;
            if (std::fabs(det) < 1e-12f) continue;
            float r = 1.0f / det;

            glm::vec3 t = (e1 * d2.y - e2 * d1.y) * r;
            glm::vec3 bt = (e2 * d1.x - e1 * d2.x) * r;

            for (int k = 0; k < 3; k++)
            {
                tan[indices[i + k]] += t;
                bitan[indices[i + k]] += bt;
            }
        }

        for (size_t i = 0; i < vertices.size(); i++)
        {
            const glm::vec3& n = vertices[i].normal;
            glm::vec3 t = tan[i] - n * glm::dot(n, tan[i]);

            if (glm::length(t) < 1e-8f)
            {
                glm::vec3 axis = std::fabs(n.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f)
                                                       : glm::vec3(0.0f, 1.0f, 0.0f);
                t = glm::cross(n, axis);
            }
            t = glm::normalize(t);

            float w = glm::dot(glm::cross(n, t), bitan[i]) < 0.0f ? -1.0f : 1.0f;
            vertices[i].tangent = glm::vec4(t, w);
        }
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

    VkFilter toFilter(int gl)
    {
        switch (gl)
        {
            case 9728:
            case 9984:
            case 9986:
                return VK_FILTER_NEAREST;
            default:
                return VK_FILTER_LINEAR;
        }
    }

    VkSamplerAddressMode toWrap(int gl)
    {
        switch (gl)
        {
            case 33071: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            case 33648: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            default:    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        }
    }

    glm::vec4 toVec4(const std::vector<double>& v, const glm::vec4& fallback)
    {
        if (v.size() == 4) return glm::vec4(v[0], v[1], v[2], v[3]);
        if (v.size() == 3) return glm::vec4(v[0], v[1], v[2], fallback.w);
        return fallback;
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

    for (const tinygltf::Sampler& s : model.samplers)
    {
        SamplerData data;
        data.magFilter = toFilter(s.magFilter);
        data.minFilter = toFilter(s.minFilter);
        data.wrapS = toWrap(s.wrapS);
        data.wrapT = toWrap(s.wrapT);
        mesh.samplers.push_back(data);
    }

    std::map<std::tuple<int, int, bool>, int> textureLookup;
    auto resolveTexture = [&](int texIndex, bool srgb) -> int
    {
        if (texIndex < 0 || texIndex >= static_cast<int>(model.textures.size())) return -1;

        const tinygltf::Texture& tex = model.textures[texIndex];
        if (tex.source < 0 || tex.source >= static_cast<int>(mesh.images.size())) return -1;
        if (mesh.images[tex.source].pixels.empty()) return -1;

        int samplerIndex = tex.sampler < static_cast<int>(mesh.samplers.size()) ? tex.sampler : -1;
        auto key = std::make_tuple(tex.source, samplerIndex, srgb);
        auto found = textureLookup.find(key);
        if (found != textureLookup.end()) return found->second;

        TextureData data;
        data.image = tex.source;
        data.sampler = samplerIndex;
        data.srgb = srgb;
        mesh.textures.push_back(data);
        int index = static_cast<int>(mesh.textures.size()) - 1;
        textureLookup[key] = index;
        return index;
    };

    for (const tinygltf::Material& m : model.materials)
    {
        Material material;
        const auto& pbr = m.pbrMetallicRoughness;

        material.params.baseColorFactor = toVec4(pbr.baseColorFactor, glm::vec4(1.0f));
        glm::vec4 emissive = toVec4(m.emissiveFactor, glm::vec4(0.0f));
        material.params.emissiveFactor = glm::vec4(emissive.x, emissive.y, emissive.z,
                                                    static_cast<float>(m.alphaCutoff));
        material.params.factors = glm::vec4(static_cast<float>(pbr.metallicFactor),
                                            static_cast<float>(pbr.roughnessFactor),
                                            static_cast<float>(m.normalTexture.scale),
                                            static_cast<float>(m.occlusionTexture.strength));

        material.textures[0] = resolveTexture(pbr.baseColorTexture.index, true);
        material.textures[1] = resolveTexture(pbr.metallicRoughnessTexture.index, false);
        material.textures[2] = resolveTexture(m.normalTexture.index, false);
        material.textures[3] = resolveTexture(m.occlusionTexture.index, false);
        material.textures[4] = resolveTexture(m.emissiveTexture.index, true);

        material.params.flags0 = glm::ivec4(material.textures[0] >= 0, material.textures[1] >= 0,
                                            material.textures[2] >= 0, material.textures[3] >= 0);
        material.params.flags1 = glm::ivec4(material.textures[4] >= 0,
                                            m.alphaMode == "MASK" ? 1 : (m.alphaMode == "BLEND" ? 2 : 0),
                                            m.doubleSided ? 1 : 0, 0);
        mesh.materials.push_back(material);
    }

    int defaultMaterial = static_cast<int>(mesh.materials.size());
    mesh.materials.push_back(Material{});

    std::map<int, std::vector<Primitive>> meshCache;

    auto loadMesh = [&](int meshIndex) -> const std::vector<Primitive>&
    {
        auto cached = meshCache.find(meshIndex);
        if (cached != meshCache.end()) return cached->second;

        std::vector<Primitive>& records = meshCache[meshIndex];

        for (const tinygltf::Primitive& prim : model.meshes[meshIndex].primitives)
        {
            if (prim.mode != -1 && prim.mode != TINYGLTF_MODE_TRIANGLES) continue;

            auto posIt = prim.attributes.find("POSITION");
            if (posIt == prim.attributes.end()) continue;

            std::vector<float> positions = readAccessor(model, model.accessors[posIt->second]);
            size_t vertexCount = positions.size() / 3;

            std::vector<Vertex> local(vertexCount);
            for (size_t i = 0; i < vertexCount; i++)
            {
                local[i].pos = { positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2] };
                local[i].color = glm::vec3(1.0f);
                local[i].texCoord = glm::vec2(0.0f);
                local[i].normal = glm::vec3(0.0f, 0.0f, 1.0f);
                local[i].tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
            }

            auto uvIt = prim.attributes.find("TEXCOORD_0");
            if (uvIt != prim.attributes.end())
            {
                std::vector<float> uvs = readAccessor(model, model.accessors[uvIt->second]);
                for (size_t i = 0; i < vertexCount && i * 2 + 1 < uvs.size(); i++)
                {
                    local[i].texCoord = { uvs[i * 2], uvs[i * 2 + 1] };
                }
            }

            auto colorIt = prim.attributes.find("COLOR_0");
            if (colorIt != prim.attributes.end())
            {
                const tinygltf::Accessor& colorAccessor = model.accessors[colorIt->second];
                int comps = tinygltf::GetNumComponentsInType(colorAccessor.type);
                std::vector<float> colors = readAccessor(model, colorAccessor);
                for (size_t i = 0; i < vertexCount && (i + 1) * comps <= colors.size(); i++)
                {
                    local[i].color = { colors[i * comps], colors[i * comps + 1], colors[i * comps + 2] };
                }
            }

            std::vector<uint32_t> localIndices;
            if (prim.indices >= 0)
            {
                localIndices = readIndices(model, model.accessors[prim.indices]);
            }
            else
            {
                localIndices.resize(vertexCount);
                for (size_t i = 0; i < vertexCount; i++) localIndices[i] = static_cast<uint32_t>(i);
            }

            localIndices.resize(localIndices.size() / 3 * 3);
            if (localIndices.empty()) continue;

            for (uint32_t index : localIndices)
            {
                if (index >= vertexCount)
                {
                    throw std::runtime_error("Index references a vertex out of range!");
                }
            }

            auto normalIt = prim.attributes.find("NORMAL");
            bool hasNormals = normalIt != prim.attributes.end();
            if (hasNormals)
            {
                std::vector<float> normals = readAccessor(model, model.accessors[normalIt->second]);
                for (size_t i = 0; i < vertexCount && i * 3 + 2 < normals.size(); i++)
                {
                    local[i].normal = { normals[i * 3], normals[i * 3 + 1], normals[i * 3 + 2] };
                }
            }
            else
            {
                generateNormals(local, localIndices);
            }

            auto tangentIt = prim.attributes.find("TANGENT");
            if (tangentIt != prim.attributes.end())
            {
                std::vector<float> tangents = readAccessor(model, model.accessors[tangentIt->second]);
                for (size_t i = 0; i < vertexCount && i * 4 + 3 < tangents.size(); i++)
                {
                    local[i].tangent = { tangents[i * 4], tangents[i * 4 + 1],
                                         tangents[i * 4 + 2], tangents[i * 4 + 3] };
                }
            }
            else
            {
                generateTangents(local, localIndices);
            }

            uint32_t vertexOffset = static_cast<uint32_t>(mesh.vertices.size());

            Primitive record{};
            record.firstIndex = static_cast<uint32_t>(mesh.indices.size());
            record.materialIndex = prim.material >= 0 && prim.material < defaultMaterial
                                    ? prim.material : defaultMaterial;

            mesh.vertices.insert(mesh.vertices.end(), local.begin(), local.end());
            for (uint32_t index : localIndices)
            {
                mesh.indices.push_back(index + vertexOffset);
            }

            record.indexCount = static_cast<uint32_t>(localIndices.size());
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
        throw std::runtime_error("glTF mesh has no triangle primitives with POSITION!");
    }

    std::cout << "  [loadGltf] SUCCESS - " << mesh.vertices.size() << " vertices, "
                << mesh.indices.size() << " indices, "
                << mesh.images.size() << " images, "
                << mesh.materials.size() << " materials, "
                << mesh.primitives.size() << " primitives.\n";
    return mesh;
}

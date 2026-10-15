#pragma once
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct Vertex 
{
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord;
    glm::vec3 normal;
    glm::vec4 tangent;

    static VkVertexInputBindingDescription getBindingDescription()
    {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 5> getAttributeDescriptions() 
    {
        std::array<VkVertexInputAttributeDescription, 5> attributeDescriptions{}; 
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, pos);
        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex, color);

        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

        attributeDescriptions[3].binding = 0;
        attributeDescriptions[3].location = 3;
        attributeDescriptions[3].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[3].offset = offsetof(Vertex, normal);

        attributeDescriptions[4].binding = 0;
        attributeDescriptions[4].location = 4;
        attributeDescriptions[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[4].offset = offsetof(Vertex, tangent);

        return attributeDescriptions;
    }
};

constexpr uint32_t MATERIAL_TEXTURE_SLOTS = 5;

enum class RenderClass
{
    Opaque,
    Transmissive,
    Blend
};

struct Primitive
{
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    int materialIndex = 0;
    glm::mat4 worldMatrix = glm::mat4(1.0f);
    bool castsShadow = true;
    RenderClass renderClass = RenderClass::Opaque;
    glm::vec3 boundsCenter{ 0.0f };
};

struct ImageData
{
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
};

struct SamplerData
{
    VkFilter magFilter = VK_FILTER_LINEAR;
    VkFilter minFilter = VK_FILTER_LINEAR;
    VkSamplerAddressMode wrapS = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode wrapT = VK_SAMPLER_ADDRESS_MODE_REPEAT;
};

struct TextureData
{
    int image = -1;
    int sampler = -1;
    bool srgb = true;
};

struct MaterialParams
{
    glm::vec4 baseColorFactor{ 1.0f };
    glm::vec4 emissiveFactor{ 0.0f, 0.0f, 0.0f, 0.5f };
    glm::vec4 factors{ 0.0f, 0.5f, 1.0f, 1.0f };
    glm::ivec4 flags0{ 0 };
    glm::ivec4 flags1{ 0 };
    glm::vec4 uvLinear[MATERIAL_TEXTURE_SLOTS] = {
        { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f },
        { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }
    };
    glm::vec4 uvOffset[MATERIAL_TEXTURE_SLOTS] = {};
    glm::vec4 transmissionParams{ 0.0f, 1.5f, 0.0f, 0.0f };
    glm::vec4 clearcoatParams{ 0.0f, 0.0f, 0.0f, 0.0f };
};

struct Material
{
    MaterialParams params;
    std::array<int, MATERIAL_TEXTURE_SLOTS> textures{ -1, -1, -1, -1, -1 };
};

struct GpuTexture
{
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<Primitive> primitives;
    std::vector<ImageData> images;
    std::vector<SamplerData> samplers;
    std::vector<TextureData> textures;
    std::vector<Material> materials;
};

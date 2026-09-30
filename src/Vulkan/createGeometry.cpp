
#include "../AlethiaV3.h"
#include <iostream>
#include <cstring>
#include <vector>

namespace
{
// same triangle as the hardcoded,  now as real data.
const std::vector<Vertex> VERTICES = {
    {{ 0.0f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}},
    {{ 0.5f,  0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}},
    {{-0.5f,  0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}}
};
const std::vector<uint32_t> INDICES = { 0,1,2 };
} // namespace

void AlethiaV3::createVertexBuffer()
{
    std::cout << "  [createVertexBuffer] Starting ... \n";
    VkDeviceSize bufferSize = sizeof(VERTICES[0]) * VERTICES.size();

    AllocatedBuffer staging = createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    void *data;
    vkMapMemory(M_DEVICE, staging.memory, 0, bufferSize, 0, &data);
    std::memcpy(data, VERTICES.data(), static_cast<size_t>(bufferSize));
    vkUnmapMemory(M_DEVICE, staging.memory);

    M_VERTEXBUFFER = createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    copyBuffer(staging.buffer, M_VERTEXBUFFER.buffer, bufferSize);
    destroyBuffer(staging);

    std::cout << "  [createVertexBuffer] SUCCESS - " << VERTICES.size() << " vertices uploaded.\n";
}

void AlethiaV3::createIndexBuffer()
{
    std::cout << "  [createIndexBuffer] Starting ... \n";

    VkDeviceSize bufferSize = sizeof(INDICES[0]) * INDICES.size();
    M_INDEXCOUNT = static_cast<uint32_t>(INDICES.size());

    AllocatedBuffer staging = createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    void* data;
    vkMapMemory(M_DEVICE, staging.memory, 0, bufferSize, 0, &data);
    std::memcpy(data, INDICES.data(), static_cast<size_t>(bufferSize));
    vkUnmapMemory(M_DEVICE, staging.memory);

    M_INDEXBUFFER = createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    copyBuffer(staging.buffer, M_INDEXBUFFER.buffer, bufferSize);
    destroyBuffer(staging);

    std::cout << "  [createIndexBuffer] SUCCESS - " << M_INDEXCOUNT << " indices uploaded.\n";
}

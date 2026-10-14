#include "../AlethiaV3.h"
#include <iostream>
#include <cstring>
#include <vector>

void AlethiaV3::uploadModelGeometry(Model& model, const MeshData& mesh)
{
    auto upload = [&](const void* data, VkDeviceSize size, VkBufferUsageFlags usage) -> AllocatedBuffer
    {
        AllocatedBuffer staging = createBuffer(
            size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        std::memcpy(staging.mapped, data, static_cast<size_t>(size));

        AllocatedBuffer device = createBuffer(
            size,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );

        copyBuffer(staging.buffer, device.buffer, size);
        destroyBuffer(staging);
        return device;
    };

    model.vertexBuffer = upload(mesh.vertices.data(),
                                sizeof(Vertex) * mesh.vertices.size(),
                                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    model.indexBuffer = upload(mesh.indices.data(),
                               sizeof(uint32_t) * mesh.indices.size(),
                               VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    model.primitives = mesh.primitives;

    std::cout << "  [uploadModelGeometry] SUCCESS - " << mesh.vertices.size() << " vertices, "
              << mesh.indices.size() << " indices uploaded.\n";
}

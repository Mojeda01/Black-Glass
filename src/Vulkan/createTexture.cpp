#include "../AlethiaV3.h"
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <map>

#define STB_IMAGE_IMPLEMENTATION
#include "../stb_image.h"

void AlethiaV3::transitionImageLayoutImmediate( VkImage image,
                                                VkImageLayout oldLayout,
                                                VkImageLayout newLayout)
{
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
            newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_2_NONE;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    }

    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
            newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    }
    else 
    {
        throw std::invalid_argument("Unsupported layout transition!");
    }

    VkDependencyInfo dependencyInfo{};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.imageMemoryBarrierCount = 1;
    dependencyInfo.pImageMemoryBarriers = &barrier;

    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);

    endSingleTimeCommands(commandBuffer);
}

void AlethiaV3::copyBufferToImage(VkBuffer buffer, VkImage image,
                                    uint32_t width, uint32_t height)
{
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { width, height, 1 };

    vkCmdCopyBufferToImage(commandBuffer, buffer, image,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    endSingleTimeCommands(commandBuffer);
}

AllocatedImage AlethiaV3::createTextureFromPixels(const unsigned char* pixels,
                                        int texWidth, int texHeight,
                                        VkFormat format)
{

    VkDeviceSize imageSize = static_cast<VkDeviceSize>(texWidth) * texHeight * 4;

    AllocatedBuffer staging = createBuffer(
        imageSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    void* data;
    vkMapMemory(M_DEVICE, staging.memory, 0, imageSize, 0, &data);
    std::memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(M_DEVICE, staging.memory);

    AllocatedImage texture = createImage(
        static_cast<uint32_t>(texWidth),
        static_cast<uint32_t>(texHeight),
        format,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT
    );

    transitionImageLayoutImmediate(
        texture.image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );
    
    copyBufferToImage(staging.buffer, texture.image,
        static_cast<uint32_t>(texWidth),
        static_cast<uint32_t>(texHeight)
    );

    transitionImageLayoutImmediate(
        texture.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );

    destroyBuffer(staging);
    std::cout << "  [createTextureFromPixels] SUCCESS - " << texWidth << "x" << texHeight
        << " texture uploaded.\n";
    return texture;
}

void AlethiaV3::createTextures(const MeshData& mesh)
{
    const unsigned char white[4] = { 255, 255, 255, 255 };
    M_FALLBACKTEXTURE = createTextureFromPixels(white, 1, 1, VK_FORMAT_R8G8B8A8_UNORM);

    for (const SamplerData& data : mesh.samplers)
    {
        M_SAMPLERS.push_back(createSamplerObject(data));
    }

    std::map<std::pair<int, bool>, size_t> imageLookup;
    M_TEXTUREIMAGES.reserve(mesh.textures.size());
    M_TEXTURES.reserve(mesh.textures.size());

    for (const TextureData& texture : mesh.textures)
    {
        auto key = std::make_pair(texture.image, texture.srgb);
        auto found = imageLookup.find(key);
        if (found == imageLookup.end())
        {
            const ImageData& image = mesh.images[texture.image];
            M_TEXTUREIMAGES.push_back(createTextureFromPixels(
                image.pixels.data(), image.width, image.height,
                texture.srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM));
            found = imageLookup.emplace(key, M_TEXTUREIMAGES.size() - 1).first;
        }

        GpuTexture gpu{};
        gpu.view = M_TEXTUREIMAGES[found->second].view;
        gpu.sampler = texture.sampler >= 0 ? M_SAMPLERS[texture.sampler] : M_TEXTURESAMPLER;
        M_TEXTURES.push_back(gpu);
    }

    M_MATERIALS = mesh.materials;
    createMaterialBuffer();
}

void AlethiaV3::createMaterialBuffer()
{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(M_PHYSICALDEVICE, &properties);

    VkDeviceSize alignment = properties.limits.minUniformBufferOffsetAlignment;
    if (alignment == 0) alignment = 1;
    M_MATERIALSTRIDE = (sizeof(MaterialParams) + alignment - 1) / alignment * alignment;

    VkDeviceSize bufferSize = M_MATERIALSTRIDE * M_MATERIALS.size();
    M_MATERIALBUFFER = createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    void* data = nullptr;
    vkMapMemory(M_DEVICE, M_MATERIALBUFFER.memory, 0, bufferSize, 0, &data);
    std::memset(data, 0, static_cast<size_t>(bufferSize));
    for (size_t i = 0; i < M_MATERIALS.size(); i++)
    {
        std::memcpy(static_cast<char*>(data) + i * M_MATERIALSTRIDE,
                    &M_MATERIALS[i].params, sizeof(MaterialParams));
    }
    vkUnmapMemory(M_DEVICE, M_MATERIALBUFFER.memory);

    std::cout << "  [createMaterialBuffer] SUCCESS - " << M_MATERIALS.size()
                << " materials uploaded.\n";
}

VkSampler AlethiaV3::createSamplerObject(const SamplerData& data)
{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(M_PHYSICALDEVICE, &properties);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = data.magFilter;
    samplerInfo.minFilter = data.minFilter;
    samplerInfo.addressModeU = data.wrapS;
    samplerInfo.addressModeV = data.wrapT;
    samplerInfo.addressModeW = data.wrapS;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = data.minFilter == VK_FILTER_NEAREST
        ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;

    VkSampler sampler = VK_NULL_HANDLE;
    if (vkCreateSampler(M_DEVICE, &samplerInfo, nullptr, &sampler) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create texture sampler!");
    }
    return sampler;
}

void AlethiaV3::createTextureSampler()
{
    M_TEXTURESAMPLER = createSamplerObject(SamplerData{});
    std::cout << "  [createTextureSampler] SUCCESS - Default sampler created.\n";
}

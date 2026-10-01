#include "../AlethiaV3.h"
#include <iostream>
#include <stdexcept>

uint32_t AlethiaV3::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(M_PHYSICALDEVICE, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
    {
        const bool typeAllowed = (typeFilter & (1u << i)) != 0;
        const bool hasProperties = 
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties;

        if (typeAllowed && hasProperties)
        {
            return i;
        }
    }
    throw std::runtime_error("Failed to find a suitable memory type!");
}

AllocatedBuffer AlethiaV3::createBuffer(    VkDeviceSize size,
                                            VkBufferUsageFlags usage,
                                            VkMemoryPropertyFlags properties)
{
    AllocatedBuffer result{};
    result.size = size;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(M_DEVICE, &bufferInfo, nullptr, &result.buffer) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(M_DEVICE, result.buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    VkMemoryAllocateFlagsInfo flagsInfo{};
    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
    {
        flagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        allocInfo.pNext = &flagsInfo;
    }

    if (vkAllocateMemory(M_DEVICE, &allocInfo, nullptr, &result.memory) != VK_SUCCESS)
    {
        vkDestroyBuffer(M_DEVICE, result.buffer, nullptr);
        throw std::runtime_error("Failed to allocate buffer memory!");
    }

    vkBindBufferMemory(M_DEVICE, result.buffer, result.memory, 0);
    return result;
}

void AlethiaV3::destroyBuffer(AllocatedBuffer& buffer)
{
    if (buffer.buffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(M_DEVICE, buffer.buffer, nullptr);
        buffer.buffer = VK_NULL_HANDLE;
    }

    if (buffer.memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(M_DEVICE, buffer.memory, nullptr);
        buffer.memory = VK_NULL_HANDLE;
    }
    buffer.size = 0;
}

VkCommandBuffer AlethiaV3::beginSingleTimeCommands()
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = M_COMMANDPOOL;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    if (vkAllocateCommandBuffers(M_DEVICE, &allocInfo, &commandBuffer) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate single-time command buffer!");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    return commandBuffer;
}

void AlethiaV3::endSingleTimeCommands(VkCommandBuffer commandBuffer)
{
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(M_GRAPHICSQUEUE, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(M_GRAPHICSQUEUE);
    vkFreeCommandBuffers(M_DEVICE, M_COMMANDPOOL, 1, &commandBuffer);
}

void AlethiaV3::copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size)
{
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, src, dst, 1, &copyRegion);
    endSingleTimeCommands(commandBuffer);
}

AllocatedImage AlethiaV3::createImage(  uint32_t width, uint32_t height, VkFormat format,
                                        VkImageTiling tiling, VkImageUsageFlags usage,
                                        VkMemoryPropertyFlags properties,
                                        VkImageAspectFlags aspectFlags)
{
    AllocatedImage result{};
    result.format = format;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(M_DEVICE, &imageInfo, nullptr, &result.image) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(M_DEVICE, result.image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(M_DEVICE, &allocInfo, nullptr, &result.memory) != VK_SUCCESS)
    {
        vkDestroyImage(M_DEVICE, result.image, nullptr);
        throw std::runtime_error("Failed to allocate image memory!");
    }

    vkBindImageMemory(M_DEVICE, result.image, result.memory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = result.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(M_DEVICE, &viewInfo, nullptr, &result.view) != VK_SUCCESS)
    {
        vkFreeMemory(M_DEVICE, result.memory, nullptr);
        vkDestroyImage(M_DEVICE, result.image, nullptr);
        throw std::runtime_error("Failed to create image view!");
    }

    return result;
}

void AlethiaV3::destroyImage(AllocatedImage& image)
{
   if (image.view != VK_NULL_HANDLE)
   {
        vkDestroyImageView(M_DEVICE, image.view, nullptr);
        image.view = VK_NULL_HANDLE;
   }

   if (image.image != VK_NULL_HANDLE)
   {
        vkDestroyImage(M_DEVICE, image.image, nullptr);
        image.image = VK_NULL_HANDLE;
   }

   if (image.memory != VK_NULL_HANDLE)
   {
        vkFreeMemory(M_DEVICE, image.memory, nullptr);
        image.memory = VK_NULL_HANDLE;
   }

   image.format = VK_FORMAT_UNDEFINED;
}

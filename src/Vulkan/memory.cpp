#include "../AlethiaV3.h"
#include <iostream>
#include <stdexcept>

void AlethiaV3::createAllocator()
{
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT |
                          VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorInfo.instance = M_INSTANCE;
    allocatorInfo.physicalDevice = M_PHYSICALDEVICE;
    allocatorInfo.device = M_DEVICE;

    if (vmaCreateAllocator(&allocatorInfo, &M_ALLOCATOR) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create VMA allocator!");
    }

    std::cout << "  [createAllocator] SUCCESS - VMA allocator created.\n";
}

void AlethiaV3::destroyAllocator()
{
    if (M_ALLOCATOR != VK_NULL_HANDLE)
    {
        vmaDestroyAllocator(M_ALLOCATOR);
        M_ALLOCATOR = VK_NULL_HANDLE;
    }
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

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.requiredFlags = properties;
    if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    {
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                          VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    VmaAllocationInfo resultInfo{};
    if (vmaCreateBuffer(M_ALLOCATOR, &bufferInfo, &allocInfo,
                        &result.buffer, &result.allocation, &resultInfo) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create buffer!");
    }

    result.mapped = resultInfo.pMappedData;
    return result;
}

void AlethiaV3::destroyBuffer(AllocatedBuffer& buffer)
{
    if (buffer.buffer != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(M_ALLOCATOR, buffer.buffer, buffer.allocation);
        buffer.buffer = VK_NULL_HANDLE;
        buffer.allocation = VK_NULL_HANDLE;
    }
    buffer.mapped = nullptr;
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
                                        VkImageAspectFlags aspectFlags,
                                        uint32_t mipLevels,
                                        uint32_t arrayLayers,
                                        VkImageCreateFlags flags,
                                        VkImageViewType viewType,
                                        VkSampleCountFlagBits samples)
{
    AllocatedImage result{};
    result.format = format;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = arrayLayers;
    imageInfo.flags = flags;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = samples;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.requiredFlags = properties;
    if (usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))
    {
        allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    }

    if (vmaCreateImage(M_ALLOCATOR, &imageInfo, &allocInfo,
                        &result.image, &result.allocation, nullptr) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create image!");
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = result.image;
    viewInfo.viewType = viewType;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = arrayLayers;

    if (vkCreateImageView(M_DEVICE, &viewInfo, nullptr, &result.view) != VK_SUCCESS)
    {
        vmaDestroyImage(M_ALLOCATOR, result.image, result.allocation);
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
        vmaDestroyImage(M_ALLOCATOR, image.image, image.allocation);
        image.image = VK_NULL_HANDLE;
        image.allocation = VK_NULL_HANDLE;
   }

   image.format = VK_FORMAT_UNDEFINED;
}

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

#include "../AlethiaV3.h"
#include <iostream>
#include <stdexcept>

void AlethiaV3::createCommandPool()
{
    std::cout << "  [createCommandPool] Starting ... \n";

    QueueFamilyIndices queueFamilyIndices = findQueueFamilies(M_PHYSICALDEVICE);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

    if (vkCreateCommandPool(M_DEVICE, &poolInfo, nullptr, &M_COMMANDPOOL) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create command pool!");
    }

    std::cout << "  [createCommandPool] SUCCESS - Command Pool created" << std::endl;
}
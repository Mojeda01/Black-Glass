#include "../AlethiaV3.h"
#include <iostream>
#include <stdexcept>

void AlethiaV3::createSyncObjects()
{
    std::cout << "  [createSyncObjects] Starting ... \n";

    M_IMAGEAVAILABLESEMAPHORES.resize(MAX_FRAMES_IN_FLIGHT);
    M_RENDERFINISHEDSEMAPHORES.resize(M_SWAPCHAINIMAGES.size());
    M_INFLIGHTFENCES.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (vkCreateSemaphore(M_DEVICE, &semaphoreInfo, nullptr, &M_IMAGEAVAILABLESEMAPHORES[i]) != VK_SUCCESS   || 
            vkCreateFence(M_DEVICE, &fenceInfo, nullptr, &M_INFLIGHTFENCES[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create synchronization objects for a frame!");
        }
    }

    for (size_t i = 0; i < M_SWAPCHAINIMAGES.size(); i++)
    {
        if (vkCreateSemaphore(M_DEVICE, &semaphoreInfo, nullptr, &M_RENDERFINISHEDSEMAPHORES[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create render-finished semaphore!");
        }
    }

    std::cout << "  [createSyncObjects] SUCCESS - Created sync objects for "
              << MAX_FRAMES_IN_FLIGHT << " frames in flight. \n";
}
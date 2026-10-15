#include "../BlackGlass.h"
#include <iostream>
#include <stdexcept>

void BlackGlass::createSyncObjects()
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
void BlackGlass::createTimestampPool()
{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(M_PHYSICALDEVICE, &properties);
    m_timestampPeriod = properties.limits.timestampPeriod;

    QueueFamilyIndices indices = findQueueFamilies(M_PHYSICALDEVICE);
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(M_PHYSICALDEVICE, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(M_PHYSICALDEVICE, &familyCount, families.data());

    uint32_t validBits = 0;
    if (indices.graphicsFamily.has_value() && indices.graphicsFamily.value() < familyCount)
    {
        validBits = families[indices.graphicsFamily.value()].timestampValidBits;
    }

    if (validBits == 0 || m_timestampPeriod <= 0.0f)
    {
        m_timestampsSupported = false;
        std::cout << "  [createTimestampPool] Timestamps unsupported on this queue, GPU timings disabled.\n";
        return;
    }

    m_timestampMask = validBits >= 64 ? ~0ull : ((1ull << validBits) - 1ull);

    VkQueryPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    poolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    poolInfo.queryCount = GPU_TIMESTAMPS * MAX_FRAMES_IN_FLIGHT;

    if (vkCreateQueryPool(M_DEVICE, &poolInfo, nullptr, &M_TIMESTAMPPOOL) != VK_SUCCESS)
    {
        M_TIMESTAMPPOOL = VK_NULL_HANDLE;
        m_timestampsSupported = false;
        std::cout << "  [createTimestampPool] Failed to create query pool, GPU timings disabled.\n";
        return;
    }

    m_timestampsSupported = true;
    std::cout << "  [createTimestampPool] SUCCESS - GPU timestamp queries ready.\n";
}

void BlackGlass::writeTimestamp(VkCommandBuffer commandBuffer, uint32_t index)
{
    if (!m_timestampsSupported || !m_gpuTimingsEnabled)
    {
        return;
    }
    vkCmdWriteTimestamp2(commandBuffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, M_TIMESTAMPPOOL,
                         M_CURRENTFRAME * GPU_TIMESTAMPS + index);
}

void BlackGlass::collectGpuTimings(uint32_t frameSlot)
{
    if (!m_timestampsSupported || !m_gpuTimingsEnabled || !m_timestampRecorded[frameSlot])
    {
        return;
    }

    uint64_t values[GPU_TIMESTAMPS] = {};
    VkResult result = vkGetQueryPoolResults(M_DEVICE, M_TIMESTAMPPOOL, frameSlot * GPU_TIMESTAMPS,
                                            GPU_TIMESTAMPS, sizeof(values), values, sizeof(uint64_t),
                                            VK_QUERY_RESULT_64_BIT);
    if (result != VK_SUCCESS)
    {
        return;
    }

    for (uint32_t i = 0; i < GPU_SEGMENTS; i++)
    {
        uint64_t begin = values[i] & m_timestampMask;
        uint64_t end = values[i + 1] & m_timestampMask;
        uint64_t delta = end >= begin ? end - begin : (m_timestampMask - begin + end + 1);
        float ms = static_cast<float>(static_cast<double>(delta) * m_timestampPeriod * 1e-6);
        m_gpuSegmentsMs[i] += (ms - m_gpuSegmentsMs[i]) * 0.1f;
    }
}

#include "../AlethiaV3.h"
#include <iostream>
#include <stdexcept>

void AlethiaV3::createCommandBuffers()
{
    std::cout << "  [createCommandBuffers] Starting ... \n";

    M_COMMANDBUFFERS.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = M_COMMANDPOOL;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(M_COMMANDBUFFERS.size());

    if(vkAllocateCommandBuffers(M_DEVICE, &allocInfo, M_COMMANDBUFFERS.data()) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate command buffers!");
    }

    std::cout << "  [createCommandBuffers] SUCCESS - Allocated " << M_COMMANDBUFFERS.size() << " command buffers.\n";
}


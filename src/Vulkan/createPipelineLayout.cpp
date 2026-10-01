#include "../AlethiaV3.h"
#include <iostream>
#include <stdexcept>

void AlethiaV3::createPipelineLayout()
{
    std::cout << "  [createPipelineLayout]  Starting ... \n";

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &M_DESCRIPTORSETLAYOUT;
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = nullptr;

    if (vkCreatePipelineLayout(M_DEVICE, &pipelineLayoutInfo, nullptr, &M_PIPELINELAYOUT) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create pipeline layout!");
    }

    std::cout << "  [createPipelineLayout] SUCCESS - Pipeline layout created.\n";
}

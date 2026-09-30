#include "../AlethiaV3.h"
#include <iostream>
#include <stdexcept>

void AlethiaV3::createPipelineLayout()
{
    std::cout << "  [createPipelineLayout]  Starting ... \n";

    // empty for now, no descriptor sets, no push constants. Nothing
    // downstream (no shaders, no uniform/texture data) needs external
    // binding yet. Extent this only when a specific requires it.

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0;
    pipelineLayoutInfo.pSetLayouts = nullptr;
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = nullptr;

    if (vkCreatePipelineLayout(M_DEVICE, &pipelineLayoutInfo, nullptr, &M_PIPELINELAYOUT) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create pipeline layout!");
    }

    std::cout << "  [createPipelineLayout] SUCCESS - Pipeline layout created.\n";
}
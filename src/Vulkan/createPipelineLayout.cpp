#include "../AlethiaV3.h"
#include <iostream>
#include <stdexcept>

void AlethiaV3::createPipelineLayout()
{
    std::cout << "  [createPipelineLayout]  Starting ... \n";

    VkPushConstantRange pushRanges[2]{};
    pushRanges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRanges[0].offset = 0;
    pushRanges[0].size = sizeof(glm::mat4);
    pushRanges[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRanges[1].offset = sizeof(glm::mat4);
    pushRanges[1].size = sizeof(glm::vec4);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &M_DESCRIPTORSETLAYOUT;
    pipelineLayoutInfo.pushConstantRangeCount = 2;
    pipelineLayoutInfo.pPushConstantRanges = pushRanges;

    if (vkCreatePipelineLayout(M_DEVICE, &pipelineLayoutInfo, nullptr, &M_PIPELINELAYOUT) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create pipeline layout!");
    }

    std::cout << "  [createPipelineLayout] SUCCESS - Pipeline layout created.\n";
}

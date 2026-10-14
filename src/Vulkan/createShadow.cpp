#include "../BlackGlass.h"
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <string>

#include <glm/gtc/matrix_transform.hpp>

namespace
{

struct ShadowPush
{
    glm::mat4 model;
    glm::mat4 lightViewProj;
};

void depthBarrier(VkCommandBuffer commandBuffer, VkImage image,
                  VkImageLayout oldLayout, VkImageLayout newLayout,
                  VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                  VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
{
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;

    VkDependencyInfo dependencyInfo{};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.imageMemoryBarrierCount = 1;
    dependencyInfo.pImageMemoryBarriers = &barrier;

    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
}

}

void BlackGlass::createShadowMap()
{
    std::cout << "  [createShadowMap] Starting ... \n";

    VkFormatProperties formatProps{};
    vkGetPhysicalDeviceFormatProperties(M_PHYSICALDEVICE, SHADOW_FORMAT, &formatProps);
    const VkFormatFeatureFlags required = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                          VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if ((formatProps.optimalTilingFeatures & required) != required)
    {
        throw std::runtime_error("Shadow map format is not supported!");
    }
    const bool linearCompare =
        (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;

    M_SHADOWMAP = createImage(
        SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, SHADOW_FORMAT, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = linearCompare ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    samplerInfo.minFilter = linearCompare ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.compareEnable = VK_TRUE;
    samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    if (vkCreateSampler(M_DEVICE, &samplerInfo, nullptr, &M_SHADOWSAMPLER) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create shadow sampler!");
    }

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(ShadowPush);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(M_DEVICE, &pipelineLayoutInfo, nullptr, &M_SHADOWPIPELINELAYOUT) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create shadow pipeline layout!");
    }

    auto shaderCode = readFile(std::string(BLACK_GLASS_SHADER_DIR) + "/shadow.spv");
    VkShaderModule shaderModule = createShaderModule(shaderCode);

    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    stage.module = shaderModule;
    stage.pName = "main";

    VkVertexInputBindingDescription binding = Vertex::getBindingDescription();
    VkVertexInputAttributeDescription attribute{};
    attribute.binding = 0;
    attribute.location = 0;
    attribute.format = VK_FORMAT_R32G32B32_SFLOAT;
    attribute.offset = offsetof(Vertex, pos);

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 1;
    vertexInput.pVertexAttributeDescriptions = &attribute;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 1.5f;
    rasterizer.depthBiasSlopeFactor = 2.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;

    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.depthAttachmentFormat = SHADOW_FORMAT;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = 1;
    pipelineInfo.pStages = &stage;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = M_SHADOWPIPELINELAYOUT;

    if (vkCreateGraphicsPipelines(M_DEVICE, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &M_SHADOWPIPELINE) != VK_SUCCESS)
    {
        vkDestroyShaderModule(M_DEVICE, shaderModule, nullptr);
        throw std::runtime_error("Failed to create shadow pipeline!");
    }
    vkDestroyShaderModule(M_DEVICE, shaderModule, nullptr);

    bakeShadowMap();
}

void BlackGlass::bakeShadowMap()
{
    glm::vec3 center = (M_SCENEMIN + M_SCENEMAX) * 0.5f;
    float radius = std::max(0.5f * glm::length(M_SCENEMAX - M_SCENEMIN), 0.01f);
    glm::vec3 lightDir = glm::normalize(M_LIGHTDIR);
    glm::vec3 up = std::abs(lightDir.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);

    glm::mat4 lightView = glm::lookAt(center + lightDir * radius * 2.0f, center, up);
    glm::mat4 lightProj = glm::ortho(-radius, radius, -radius, radius, 0.0f, radius * 4.0f);
    M_LIGHTVIEWPROJ = lightProj * lightView;
    M_SHADOWNORMALBIAS = 2.0f * (2.0f * radius / static_cast<float>(SHADOW_MAP_SIZE));

    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    depthBarrier(commandBuffer, M_SHADOWMAP.image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = M_SHADOWMAP.view;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

    VkRenderingInfo passInfo{};
    passInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    passInfo.renderArea.extent = { SHADOW_MAP_SIZE, SHADOW_MAP_SIZE };
    passInfo.layerCount = 1;
    passInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(commandBuffer, &passInfo);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, M_SHADOWPIPELINE);

    VkViewport viewport{};
    viewport.width = static_cast<float>(SHADOW_MAP_SIZE);
    viewport.height = static_cast<float>(SHADOW_MAP_SIZE);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = { SHADOW_MAP_SIZE, SHADOW_MAP_SIZE };
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    uint32_t drawn = 0;
    for (const auto& model : M_MODELS)
    {
        if (!model->visible || model->ghost)
        {
            continue;
        }

        VkBuffer vertexBuffers[] = { model->asset->vertexBuffer.buffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(commandBuffer, model->asset->indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

        for (const Primitive& prim : model->asset->primitives)
        {
            if (!prim.castsShadow)
            {
                continue;
            }

            ShadowPush push{ model->transform * prim.worldMatrix, M_LIGHTVIEWPROJ };
            vkCmdPushConstants(commandBuffer, M_SHADOWPIPELINELAYOUT, VK_SHADER_STAGE_VERTEX_BIT,
                                0, sizeof(ShadowPush), &push);
            vkCmdDrawIndexed(commandBuffer, prim.indexCount, 1, prim.firstIndex, 0, 0);
            drawn++;
        }
    }

    vkCmdEndRendering(commandBuffer);

    depthBarrier(commandBuffer, M_SHADOWMAP.image,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    endSingleTimeCommands(commandBuffer);

    std::cout << "  [bakeShadowMap] " << SHADOW_MAP_SIZE << "x" << SHADOW_MAP_SIZE
              << " shadow map baked from " << drawn << " primitives.\n";
}

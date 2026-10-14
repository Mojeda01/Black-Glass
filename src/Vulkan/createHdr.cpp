#include "../BlackGlass.h"
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <string>

void BlackGlass::pickMsaaSamples()
{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(M_PHYSICALDEVICE, &properties);

    VkSampleCountFlags counts = properties.limits.framebufferColorSampleCounts &
                                properties.limits.framebufferDepthSampleCounts;

    if (counts & VK_SAMPLE_COUNT_4_BIT)
    {
        M_MSAASAMPLES = VK_SAMPLE_COUNT_4_BIT;
    }
    else if (counts & VK_SAMPLE_COUNT_2_BIT)
    {
        M_MSAASAMPLES = VK_SAMPLE_COUNT_2_BIT;
    }
    else
    {
        M_MSAASAMPLES = VK_SAMPLE_COUNT_1_BIT;
    }

    std::cout << "  [pickMsaaSamples] Using " << static_cast<int>(M_MSAASAMPLES) << "x MSAA.\n";
}

void BlackGlass::createHdrResources()
{
    const uint32_t width = M_SWAPCHAINEXTENT.width;
    const uint32_t height = M_SWAPCHAINEXTENT.height;

    M_HDRCOLOR = createImage(
        width, height, HDR_FORMAT, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

    M_SCENECOPYMIPS = 1;
    for (uint32_t largest = std::max(width, height); largest > 1; largest >>= 1)
    {
        M_SCENECOPYMIPS++;
    }

    M_SCENECOPY = createImage(
        width, height, HDR_FORMAT, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT, M_SCENECOPYMIPS);

    {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_NONE;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = M_SCENECOPY.image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = M_SCENECOPYMIPS;
        barrier.subresourceRange.layerCount = 1;

        VkDependencyInfo dependencyInfo{};
        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.imageMemoryBarrierCount = 1;
        dependencyInfo.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);

        endSingleTimeCommands(commandBuffer);
    }

    if (M_MSAASAMPLES != VK_SAMPLE_COUNT_1_BIT)
    {
        M_MSAACOLOR = createImage(
            width, height, HDR_FORMAT, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
            1, 1, 0, VK_IMAGE_VIEW_TYPE_2D, M_MSAASAMPLES);
    }

    M_BLOOMIMAGES.clear();
    for (uint32_t level = 0; level < BLOOM_MAX_LEVELS; level++)
    {
        uint32_t levelWidth = width >> (level + 1);
        uint32_t levelHeight = height >> (level + 1);
        if (levelWidth == 0 || levelHeight == 0)
        {
            break;
        }

        M_BLOOMIMAGES.push_back(createImage(
            levelWidth, levelHeight, HDR_FORMAT, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT));
    }

    std::cout << "  [createHdrResources] SUCCESS - HDR target, "
              << (M_MSAASAMPLES != VK_SAMPLE_COUNT_1_BIT ? "MSAA target, " : "")
              << M_BLOOMIMAGES.size() << " bloom levels.\n";
}

void BlackGlass::destroyHdrResources()
{
    for (auto& image : M_BLOOMIMAGES)
    {
        destroyImage(image);
    }
    M_BLOOMIMAGES.clear();
    destroyImage(M_MSAACOLOR);
    destroyImage(M_HDRCOLOR);
    destroyImage(M_SCENECOPY);
}

void BlackGlass::createPostResources()
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    if (vkCreateSampler(M_DEVICE, &samplerInfo, nullptr, &M_POSTSAMPLER) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create post-process sampler!");
    }

    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    if (vkCreateSampler(M_DEVICE, &samplerInfo, nullptr, &M_SCENESAMPLER) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create scene color sampler!");
    }

    VkDescriptorSetLayoutBinding bindings[2]{};
    for (uint32_t i = 0; i < 2; i++)
    {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(M_DEVICE, &layoutInfo, nullptr, &M_POSTDESCRIPTORLAYOUT) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create post-process descriptor set layout!");
    }

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(glm::vec4);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &M_POSTDESCRIPTORLAYOUT;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(M_DEVICE, &pipelineLayoutInfo, nullptr, &M_POSTPIPELINELAYOUT) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create post-process pipeline layout!");
    }

    auto shaderCode = readFile(std::string(BLACK_GLASS_SHADER_DIR) + "/post.spv");
    VkShaderModule shaderModule = createShaderModule(shaderCode);

    auto build = [&](const char* fragmentEntry, VkFormat colorFormat, bool additive) -> VkPipeline
    {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = shaderModule;
        stages[0].pName = "fullscreenVertex";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = shaderModule;
        stages[1].pName = fragmentEntry;

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

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

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        if (additive)
        {
            blendAttachment.blendEnable = VK_TRUE;
            blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        }

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &blendAttachment;

        VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates = dynamicStates;

        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &colorFormat;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = &renderingInfo;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = M_POSTPIPELINELAYOUT;

        VkPipeline pipeline = VK_NULL_HANDLE;
        if (vkCreateGraphicsPipelines(M_DEVICE, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS)
        {
            throw std::runtime_error(std::string("Failed to create post-process pipeline: ") + fragmentEntry);
        }
        return pipeline;
    };

    M_BLOOMDOWNPIPELINE = build("bloomDownFragment", HDR_FORMAT, false);
    M_BLOOMUPPIPELINE = build("bloomUpFragment", HDR_FORMAT, true);
    M_COMPOSITEPIPELINE = build("compositeFragment", M_SWAPCHAINIMAGEFORMAT, false);

    vkDestroyShaderModule(M_DEVICE, shaderModule, nullptr);

    std::cout << "  [createPostResources] SUCCESS - Bloom and composite pipelines created.\n";
}

void BlackGlass::adjustExposure(float stops)
{
    m_settings.exposure = std::clamp(m_settings.exposure * std::exp2(stops), 0.05f, 16.0f);
    std::cout << "[exposure] " << m_settings.exposure << "\n";
}

void BlackGlass::toggleBloom()
{
    m_settings.bloom = !m_settings.bloom;
    std::cout << "[bloom] " << (m_settings.bloom ? "on" : "off") << "\n";
}

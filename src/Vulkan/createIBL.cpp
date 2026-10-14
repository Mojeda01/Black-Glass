#include "../BlackGlass.h"
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/gtc/packing.hpp>

#include "../stb_image.h"

namespace
{

struct IblPush
{
    uint32_t size;
    uint32_t useEquirect;
    float roughness;
    uint32_t sampleCount;
    float srcSize;
    float envScale;
    float pad0;
    float pad1;
};

void imageBarrier(VkCommandBuffer commandBuffer, VkImage image,
                  VkImageLayout oldLayout, VkImageLayout newLayout,
                  VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                  VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
                  uint32_t baseMip, uint32_t levelCount, uint32_t layerCount)
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
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = baseMip;
    barrier.subresourceRange.levelCount = levelCount;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = layerCount;

    VkDependencyInfo dependencyInfo{};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.imageMemoryBarrierCount = 1;
    dependencyInfo.pImageMemoryBarriers = &barrier;

    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
}

uint32_t mipCountFor(uint32_t size)
{
    uint32_t levels = 1;
    while (size > 1)
    {
        size >>= 1;
        levels++;
    }
    return levels;
}

bool formatSupports(VkPhysicalDevice device, VkFormat format, VkFormatFeatureFlags required)
{
    VkFormatProperties props{};
    vkGetPhysicalDeviceFormatProperties(device, format, &props);
    return (props.optimalTilingFeatures & required) == required;
}

}

void BlackGlass::createIBL()
{
    std::cout << "  [createIBL] Starting ... \n";

    const VkFormatFeatureFlags cubeFeatures =
        VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
        VK_FORMAT_FEATURE_BLIT_SRC_BIT |
        VK_FORMAT_FEATURE_BLIT_DST_BIT;
    if (!formatSupports(M_PHYSICALDEVICE, HDR_FORMAT, cubeFeatures))
    {
        throw std::runtime_error("HDR cubemap format lacks storage/blit/filter support!");
    }
    if (!formatSupports(M_PHYSICALDEVICE, VK_FORMAT_R16G16_SFLOAT,
            VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
    {
        throw std::runtime_error("BRDF LUT format lacks storage/filter support!");
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    if (vkCreateSampler(M_DEVICE, &samplerInfo, nullptr, &M_IBLSAMPLER) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create IBL sampler!");
    }

    const uint32_t envMips = mipCountFor(IBL_ENV_SIZE);
    const uint32_t prefilterMips = mipCountFor(IBL_PREFILTER_SIZE);
    M_IBLMAXMIP = static_cast<float>(prefilterMips - 1);

    M_IBLENV = createImage(
        IBL_ENV_SIZE, IBL_ENV_SIZE, HDR_FORMAT, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
        envMips, 6, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, VK_IMAGE_VIEW_TYPE_CUBE);

    M_IBLIRRADIANCE = createImage(
        IBL_IRRADIANCE_SIZE, IBL_IRRADIANCE_SIZE, HDR_FORMAT, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
        1, 6, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, VK_IMAGE_VIEW_TYPE_CUBE);

    M_IBLPREFILTER = createImage(
        IBL_PREFILTER_SIZE, IBL_PREFILTER_SIZE, HDR_FORMAT, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
        prefilterMips, 6, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, VK_IMAGE_VIEW_TYPE_CUBE);

    M_IBLBRDF = createImage(
        IBL_BRDF_SIZE, IBL_BRDF_SIZE, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

    AllocatedImage equirect{};
    AllocatedBuffer equirectStaging{};
    uint32_t equirectWidth = 0;
    uint32_t equirectHeight = 0;
    {
        std::string hdrPath = std::string(BLACK_GLASS_ASSET_DIR) + "/environment.hdr";
        if (std::filesystem::exists(hdrPath))
        {
            int width = 0, height = 0, channels = 0;
            float* pixels = stbi_loadf(hdrPath.c_str(), &width, &height, &channels, 4);
            if (pixels)
            {
                equirectWidth = static_cast<uint32_t>(width);
                equirectHeight = static_cast<uint32_t>(height);
                size_t texelCount = static_cast<size_t>(width) * height * 4;
                VkDeviceSize stagingSize = texelCount * sizeof(uint16_t);

                equirectStaging = createBuffer(
                    stagingSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

                uint16_t* halves = static_cast<uint16_t*>(equirectStaging.mapped);
                for (size_t i = 0; i < texelCount; i++)
                {
                    float value = pixels[i];
                    if (!(value > 0.0f)) value = 0.0f;
                    if (value > 65000.0f) value = 65000.0f;
                    halves[i] = glm::packHalf1x16(value);
                }
                stbi_image_free(pixels);

                equirect = createImage(
                    equirectWidth, equirectHeight, HDR_FORMAT, VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
                M_IBLFROMFILE = true;
                std::cout << "  [createIBL] Loaded environment.hdr (" << width << "x" << height << ").\n";
            }
        }
    }

    VkDescriptorSetLayoutBinding bindings[4]{};
    for (uint32_t i = 0; i < 4; i++)
    {
        bindings[i].binding = i;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[i].descriptorType = i < 2 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                                           : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    layoutInfo.bindingCount = 4;
    layoutInfo.pBindings = bindings;

    VkDescriptorSetLayout computeSetLayout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(M_DEVICE, &layoutInfo, nullptr, &computeSetLayout) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create IBL descriptor set layout!");
    }

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(IblPush);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &computeSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;

    VkPipelineLayout computeLayout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(M_DEVICE, &pipelineLayoutInfo, nullptr, &computeLayout) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create IBL pipeline layout!");
    }

    auto shaderCode = readFile(std::string(BLACK_GLASS_SHADER_DIR) + "/ibl.spv");
    VkShaderModule shaderModule = createShaderModule(shaderCode);

    auto makePipeline = [&](const char* entry) -> VkPipeline
    {
        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.stage.module = shaderModule;
        pipelineInfo.stage.pName = entry;
        pipelineInfo.layout = computeLayout;

        VkPipeline pipeline = VK_NULL_HANDLE;
        if (vkCreateComputePipelines(M_DEVICE, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS)
        {
            throw std::runtime_error(std::string("Failed to create compute pipeline: ") + entry);
        }
        return pipeline;
    };

    VkPipeline envPipeline = makePipeline("genEnvironment");
    VkPipeline irradiancePipeline = makePipeline("genIrradiance");
    VkPipeline prefilterPipeline = makePipeline("genPrefilter");
    VkPipeline brdfPipeline = makePipeline("genBrdfLut");

    std::vector<VkImageView> transientViews;
    auto makeView = [&](const AllocatedImage& image, VkImageViewType type,
                        uint32_t baseMip, uint32_t levelCount, uint32_t layerCount) -> VkImageView
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image.image;
        viewInfo.viewType = type;
        viewInfo.format = image.format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = baseMip;
        viewInfo.subresourceRange.levelCount = levelCount;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = layerCount;

        VkImageView view = VK_NULL_HANDLE;
        if (vkCreateImageView(M_DEVICE, &viewInfo, nullptr, &view) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create IBL image view!");
        }
        transientViews.push_back(view);
        return view;
    };

    VkCommandBuffer cmd = beginSingleTimeCommands();

    VkImageView equirectView = M_FALLBACKTEXTURE.view;
    if (M_IBLFROMFILE)
    {
        imageBarrier(cmd, equirect.image,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            0, 1, 1);

        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = { equirectWidth, equirectHeight, 1 };
        vkCmdCopyBufferToImage(cmd, equirectStaging.buffer, equirect.image,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        imageBarrier(cmd, equirect.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            0, 1, 1);
        equirectView = equirect.view;
    }

    auto dispatch = [&](VkPipeline pipeline, VkImageView sampled2D, VkImageView sampledCube,
                        VkImageView storage, uint32_t storageBinding, const IblPush& push,
                        uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

        VkDescriptorImageInfo infos[3]{};
        VkWriteDescriptorSet writes[3]{};
        uint32_t writeCount = 0;

        if (sampled2D != VK_NULL_HANDLE)
        {
            infos[writeCount].sampler = M_IBLSAMPLER;
            infos[writeCount].imageView = sampled2D;
            infos[writeCount].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            writes[writeCount].dstBinding = 0;
            writes[writeCount].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[writeCount].pImageInfo = &infos[writeCount];
            writeCount++;
        }
        if (sampledCube != VK_NULL_HANDLE)
        {
            infos[writeCount].sampler = M_IBLSAMPLER;
            infos[writeCount].imageView = sampledCube;
            infos[writeCount].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            writes[writeCount].dstBinding = 1;
            writes[writeCount].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[writeCount].pImageInfo = &infos[writeCount];
            writeCount++;
        }
        infos[writeCount].imageView = storage;
        infos[writeCount].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        writes[writeCount].dstBinding = storageBinding;
        writes[writeCount].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[writeCount].pImageInfo = &infos[writeCount];
        writeCount++;

        for (uint32_t i = 0; i < writeCount; i++)
        {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].descriptorCount = 1;
        }

        M_PUSHDESCRIPTORFN(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computeLayout, 0, writeCount, writes);
        vkCmdPushConstants(cmd, computeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(IblPush), &push);
        vkCmdDispatch(cmd, groupsX, groupsY, groupsZ);
    };

    auto groupsFor = [](uint32_t size) { return (size + 7) / 8; };

    imageBarrier(cmd, M_IBLENV.image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        0, 1, 6);
    if (envMips > 1)
    {
        imageBarrier(cmd, M_IBLENV.image,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            1, envMips - 1, 6);
    }

    {
        IblPush push{};
        push.size = IBL_ENV_SIZE;
        push.useEquirect = M_IBLFROMFILE ? 1u : 0u;
        push.envScale = 1.0f;
        VkImageView envMip0 = makeView(M_IBLENV, VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 1, 6);
        dispatch(envPipeline, equirectView, VK_NULL_HANDLE, envMip0, 2, push,
                    groupsFor(IBL_ENV_SIZE), groupsFor(IBL_ENV_SIZE), 6);
    }

    imageBarrier(cmd, M_IBLENV.image,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
        0, 1, 6);

    int32_t mipSize = static_cast<int32_t>(IBL_ENV_SIZE);
    for (uint32_t level = 1; level < envMips; level++)
    {
        int32_t nextSize = std::max(mipSize / 2, 1);

        VkImageBlit blit{};
        blit.srcOffsets[1] = { mipSize, mipSize, 1 };
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = level - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 6;
        blit.dstOffsets[1] = { nextSize, nextSize, 1 };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = level;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 6;

        vkCmdBlitImage(cmd,
            M_IBLENV.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            M_IBLENV.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit, VK_FILTER_LINEAR);

        imageBarrier(cmd, M_IBLENV.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
            level, 1, 6);

        mipSize = nextSize;
    }

    imageBarrier(cmd, M_IBLENV.image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        0, envMips, 6);

    imageBarrier(cmd, M_IBLIRRADIANCE.image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        0, 1, 6);
    imageBarrier(cmd, M_IBLPREFILTER.image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        0, prefilterMips, 6);
    imageBarrier(cmd, M_IBLBRDF.image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        0, 1, 1);

    {
        IblPush push{};
        push.size = IBL_IRRADIANCE_SIZE;
        push.sampleCount = 1024;
        push.srcSize = static_cast<float>(IBL_ENV_SIZE);
        VkImageView out = makeView(M_IBLIRRADIANCE, VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 1, 6);
        dispatch(irradiancePipeline, VK_NULL_HANDLE, M_IBLENV.view, out, 2, push,
                    groupsFor(IBL_IRRADIANCE_SIZE), groupsFor(IBL_IRRADIANCE_SIZE), 6);
    }

    for (uint32_t mip = 0; mip < prefilterMips; mip++)
    {
        uint32_t size = std::max(IBL_PREFILTER_SIZE >> mip, 1u);
        IblPush push{};
        push.size = size;
        push.roughness = prefilterMips > 1 ? static_cast<float>(mip) / static_cast<float>(prefilterMips - 1) : 0.0f;
        push.sampleCount = 256;
        push.srcSize = static_cast<float>(IBL_ENV_SIZE);
        VkImageView out = makeView(M_IBLPREFILTER, VK_IMAGE_VIEW_TYPE_2D_ARRAY, mip, 1, 6);
        dispatch(prefilterPipeline, VK_NULL_HANDLE, M_IBLENV.view, out, 2, push,
                    groupsFor(size), groupsFor(size), 6);
    }

    {
        IblPush push{};
        push.size = IBL_BRDF_SIZE;
        push.sampleCount = 512;
        VkImageView out = makeView(M_IBLBRDF, VK_IMAGE_VIEW_TYPE_2D, 0, 1, 1);
        dispatch(brdfPipeline, VK_NULL_HANDLE, VK_NULL_HANDLE, out, 3, push,
                    groupsFor(IBL_BRDF_SIZE), groupsFor(IBL_BRDF_SIZE), 1);
    }

    imageBarrier(cmd, M_IBLIRRADIANCE.image,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        0, 1, 6);
    imageBarrier(cmd, M_IBLPREFILTER.image,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        0, prefilterMips, 6);
    imageBarrier(cmd, M_IBLBRDF.image,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        0, 1, 1);

    endSingleTimeCommands(cmd);

    for (VkImageView view : transientViews)
    {
        vkDestroyImageView(M_DEVICE, view, nullptr);
    }
    vkDestroyPipeline(M_DEVICE, envPipeline, nullptr);
    vkDestroyPipeline(M_DEVICE, irradiancePipeline, nullptr);
    vkDestroyPipeline(M_DEVICE, prefilterPipeline, nullptr);
    vkDestroyPipeline(M_DEVICE, brdfPipeline, nullptr);
    vkDestroyShaderModule(M_DEVICE, shaderModule, nullptr);
    vkDestroyPipelineLayout(M_DEVICE, computeLayout, nullptr);
    vkDestroyDescriptorSetLayout(M_DEVICE, computeSetLayout, nullptr);
    destroyBuffer(equirectStaging);
    destroyImage(equirect);

    std::cout << "  [createIBL] SUCCESS - Environment, irradiance, prefilter and BRDF LUT baked.\n";
}

#include "../BlackGlass.h"
#include "barrier.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

#include <glm/gtc/matrix_transform.hpp>

namespace
{

constexpr float AMBIENT_MIN_CELL = 0.5f;
constexpr double AMBIENT_QUIET_SECONDS = 0.5;
constexpr float AMBIENT_BIAS_TEXELS = 1.0f;
constexpr float AMBIENT_BIAS_ABSOLUTE = 0.02f;
constexpr uint32_t AMBIENT_MAX_DIM = 1024;
constexpr float AMBIENT_PAD_SIDE = 2.0f;
constexpr float AMBIENT_PAD_BELOW = 1.0f;
constexpr float AMBIENT_PAD_ABOVE = 3.0f;

const glm::vec3 AMBIENT_AXES[6] = {
    { 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f }
};

glm::vec3 ambientDirection(uint32_t index)
{
    const float goldenAngle = 2.39996323f;
    float y = 1.0f - 2.0f * (static_cast<float>(index) + 0.5f) / static_cast<float>(AMBIENT_DIRECTIONS);
    float r = std::sqrt(std::max(0.0f, 1.0f - y * y));
    float phi = goldenAngle * static_cast<float>(index);
    return glm::vec3(std::cos(phi) * r, y, std::sin(phi) * r);
}

void clearWhite(VkCommandBuffer commandBuffer, VkImage image)
{
    colorBarrier(commandBuffer, image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

    VkClearColorValue white{};
    white.float32[0] = 1.0f;
    white.float32[1] = 1.0f;
    white.float32[2] = 1.0f;
    white.float32[3] = 1.0f;

    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1;
    range.layerCount = 1;
    vkCmdClearColorImage(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &white, 1, &range);

    colorBarrier(commandBuffer, image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

VkWriteDescriptorSet bufferWrite(uint32_t binding, VkDescriptorType type, const VkDescriptorBufferInfo* info)
{
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstBinding = binding;
    write.descriptorCount = 1;
    write.descriptorType = type;
    write.pBufferInfo = info;
    return write;
}

}

AllocatedImage BlackGlass::createVolumeImage(const glm::uvec3& dims)
{
    AllocatedImage result{};
    result.format = VK_FORMAT_R8G8B8A8_UNORM;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_3D;
    imageInfo.extent = { dims.x, dims.y, dims.z };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = result.format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    if (vmaCreateImage(M_ALLOCATOR, &imageInfo, &allocInfo,
                        &result.image, &result.allocation, nullptr) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create ambient volume image!");
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = result.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
    viewInfo.format = result.format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(M_DEVICE, &viewInfo, nullptr, &result.view) != VK_SUCCESS)
    {
        vmaDestroyImage(M_ALLOCATOR, result.image, result.allocation);
        throw std::runtime_error("Failed to create ambient volume image view!");
    }

    return result;
}

void BlackGlass::createAmbientVolume()
{
    std::cout << "  [createAmbientVolume] Starting ... \n";

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
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    if (vkCreateSampler(M_DEVICE, &samplerInfo, nullptr, &M_AMBIENTSAMPLER) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create ambient volume sampler!");
    }

    M_AMBIENTDEPTH = createImage(
        AMBIENT_DEPTH_SIZE, AMBIENT_DEPTH_SIZE, SHADOW_FORMAT, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_DEPTH_BIT,
        1, AMBIENT_DIRS_PER_STEP, 0, VK_IMAGE_VIEW_TYPE_2D_ARRAY);

    for (uint32_t layer = 0; layer < AMBIENT_DIRS_PER_STEP; layer++)
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = M_AMBIENTDEPTH.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = SHADOW_FORMAT;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = layer;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(M_DEVICE, &viewInfo, nullptr, &M_AMBIENTDEPTHVIEWS[layer]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create ambient depth layer view!");
        }
    }

    for (AllocatedBuffer& buffer : M_AMBIENTBAKEUBO)
    {
        buffer = createBuffer(sizeof(AmbientBakeParams), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }

    VkDescriptorSetLayoutBinding bindings[5]{};
    const VkDescriptorType types[5] = {
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
    };
    for (uint32_t i = 0; i < 5; i++)
    {
        bindings[i].binding = i;
        bindings[i].descriptorType = types[i];
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    layoutInfo.bindingCount = 5;
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(M_DEVICE, &layoutInfo, nullptr, &M_AMBIENTSETLAYOUT) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create ambient descriptor set layout!");
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &M_AMBIENTSETLAYOUT;
    if (vkCreatePipelineLayout(M_DEVICE, &pipelineLayoutInfo, nullptr, &M_AMBIENTPIPELINELAYOUT) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create ambient pipeline layout!");
    }

    auto shaderCode = readFile(std::string(BLACK_GLASS_SHADER_DIR) + "/ambient.spv");
    VkShaderModule shaderModule = createShaderModule(shaderCode);

    auto makePipeline = [&](const char* entry) -> VkPipeline
    {
        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.stage.module = shaderModule;
        pipelineInfo.stage.pName = entry;
        pipelineInfo.layout = M_AMBIENTPIPELINELAYOUT;

        VkPipeline pipeline = VK_NULL_HANDLE;
        if (vkCreateComputePipelines(M_DEVICE, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS)
        {
            vkDestroyShaderModule(M_DEVICE, shaderModule, nullptr);
            throw std::runtime_error(std::string("Failed to create compute pipeline: ") + entry);
        }
        return pipeline;
    };

    M_AMBIENTACCUMPIPELINE = makePipeline("ambientAccumulate");
    M_AMBIENTFINALIZEPIPELINE = makePipeline("ambientFinalize");
    vkDestroyShaderModule(M_DEVICE, shaderModule, nullptr);

    std::array<float, 6> weightSums{};
    for (uint32_t i = 0; i < AMBIENT_DIRECTIONS; i++)
    {
        glm::vec3 d = ambientDirection(i);
        for (uint32_t axis = 0; axis < 6; axis++)
        {
            weightSums[axis] += std::max(glm::dot(d, AMBIENT_AXES[axis]), 0.0f);
        }
    }
    for (uint32_t axis = 0; axis < 6; axis++)
    {
        m_ambientInvWeight[axis] = weightSums[axis] > 1e-4f ? 1.0f / weightSums[axis] : 0.0f;
    }

    for (AmbientVolume& volume : m_ambientVolumes)
    {
        volume.imageA = createVolumeImage(glm::uvec3(1));
        volume.imageB = createVolumeImage(glm::uvec3(1));
    }

    VkCommandBuffer commandBuffer = beginSingleTimeCommands();
    for (AmbientVolume& volume : m_ambientVolumes)
    {
        clearWhite(commandBuffer, volume.imageA.image);
        clearWhite(commandBuffer, volume.imageB.image);
    }
    endSingleTimeCommands(commandBuffer);

    m_ambientLive = 0;
    m_ambientBack = 1;
    m_ambientValid = false;
    m_ambientAllocated = false;
    m_ambientBaking = false;
    m_ambientDirty = true;
    m_ambientDirtyTime = 0.0;
    m_ambientStep = {};
}

void BlackGlass::releaseAmbientVolumes()
{
    for (AmbientVolume& volume : m_ambientVolumes)
    {
        destroyImage(volume.imageA);
        destroyImage(volume.imageB);
    }
    destroyBuffer(M_AMBIENTSCRATCH);
    destroyBuffer(M_AMBIENTPACKEDA);
    destroyBuffer(M_AMBIENTPACKEDB);
    m_ambientAllocated = false;
}

void BlackGlass::destroyAmbientVolume()
{
    if (M_AMBIENTACCUMPIPELINE != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(M_DEVICE, M_AMBIENTACCUMPIPELINE, nullptr);
        M_AMBIENTACCUMPIPELINE = VK_NULL_HANDLE;
    }
    if (M_AMBIENTFINALIZEPIPELINE != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(M_DEVICE, M_AMBIENTFINALIZEPIPELINE, nullptr);
        M_AMBIENTFINALIZEPIPELINE = VK_NULL_HANDLE;
    }
    if (M_AMBIENTPIPELINELAYOUT != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(M_DEVICE, M_AMBIENTPIPELINELAYOUT, nullptr);
        M_AMBIENTPIPELINELAYOUT = VK_NULL_HANDLE;
    }
    if (M_AMBIENTSETLAYOUT != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(M_DEVICE, M_AMBIENTSETLAYOUT, nullptr);
        M_AMBIENTSETLAYOUT = VK_NULL_HANDLE;
    }
    if (M_AMBIENTSAMPLER != VK_NULL_HANDLE)
    {
        vkDestroySampler(M_DEVICE, M_AMBIENTSAMPLER, nullptr);
        M_AMBIENTSAMPLER = VK_NULL_HANDLE;
    }

    for (VkImageView& view : M_AMBIENTDEPTHVIEWS)
    {
        if (view != VK_NULL_HANDLE)
        {
            vkDestroyImageView(M_DEVICE, view, nullptr);
            view = VK_NULL_HANDLE;
        }
    }
    destroyImage(M_AMBIENTDEPTH);

    for (AllocatedBuffer& buffer : M_AMBIENTBAKEUBO)
    {
        destroyBuffer(buffer);
    }

    releaseAmbientVolumes();
}

void BlackGlass::allocateAmbientVolumes(const glm::vec3& needMin, const glm::vec3& needMax)
{
    glm::vec3 extent = needMax - needMin;
    glm::vec3 slack = glm::max(extent * 0.1f, glm::vec3(1.0f));
    glm::vec3 lo = needMin - slack;
    glm::vec3 hi = needMax + slack;
    glm::vec3 size = hi - lo;

    float cell = AMBIENT_MIN_CELL;
    glm::uvec3 dims(1);
    for (;;)
    {
        glm::ivec3 counts = glm::max(glm::ivec3(glm::ceil(size / cell)), glm::ivec3(1));
        dims = glm::uvec3(counts);
        uint64_t voxels = static_cast<uint64_t>(dims.x) * dims.y * dims.z;
        if (voxels <= AMBIENT_MAX_VOXELS && std::max({ dims.x, dims.y, dims.z }) <= AMBIENT_MAX_DIM)
        {
            break;
        }
        cell *= 1.1f;
    }

    vkDeviceWaitIdle(M_DEVICE);
    releaseAmbientVolumes();

    for (AmbientVolume& volume : m_ambientVolumes)
    {
        volume.imageA = createVolumeImage(dims);
        volume.imageB = createVolumeImage(dims);
    }

    VkCommandBuffer commandBuffer = beginSingleTimeCommands();
    for (AmbientVolume& volume : m_ambientVolumes)
    {
        clearWhite(commandBuffer, volume.imageA.image);
        clearWhite(commandBuffer, volume.imageB.image);
    }
    endSingleTimeCommands(commandBuffer);

    const VkDeviceSize voxelCount = static_cast<VkDeviceSize>(dims.x) * dims.y * dims.z;
    M_AMBIENTSCRATCH = createBuffer(voxelCount * 6 * sizeof(float),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    M_AMBIENTPACKEDA = createBuffer(voxelCount * sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    M_AMBIENTPACKEDB = createBuffer(voxelCount * sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    m_ambientBakeMin = lo;
    m_ambientBakeDims = dims;
    m_ambientBakeCell = cell;
    m_ambientAllocated = true;
    m_ambientValid = false;
    m_ambientLive = 0;
    m_ambientBack = 1;

    std::cout << "  [allocateAmbientVolumes] " << dims.x << "x" << dims.y << "x" << dims.z
              << " voxels, cell " << cell << " m.\n";
}

bool BlackGlass::beginAmbientBake()
{
    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(std::numeric_limits<float>::lowest());
    bool any = false;

    for (const auto& model : M_MODELS)
    {
        if (!model->visible || model->ghost || model->light || model->spawn)
        {
            continue;
        }
        any = true;
        lo = glm::min(lo, model->worldMin);
        hi = glm::max(hi, model->worldMax);
    }

    if (!any)
    {
        m_ambientDirty = false;
        return false;
    }

    glm::vec3 needMin = lo - glm::vec3(AMBIENT_PAD_SIDE, AMBIENT_PAD_BELOW, AMBIENT_PAD_SIDE);
    glm::vec3 needMax = hi + glm::vec3(AMBIENT_PAD_SIDE, AMBIENT_PAD_ABOVE, AMBIENT_PAD_SIDE);

    bool fits = false;
    if (m_ambientAllocated)
    {
        glm::vec3 volumeMax = m_ambientBakeMin + glm::vec3(m_ambientBakeDims) * m_ambientBakeCell;
        fits = glm::all(glm::greaterThanEqual(needMin, m_ambientBakeMin)) &&
               glm::all(glm::lessThanEqual(needMax, volumeMax));
    }
    if (!fits)
    {
        allocateAmbientVolumes(needMin, needMax);
    }

    m_ambientDirty = false;
    m_ambientBaking = true;
    m_ambientNextDir = 0;
    return true;
}

void BlackGlass::updateAmbient()
{
    if (M_AMBIENTACCUMPIPELINE == VK_NULL_HANDLE || !m_settings.ambientOcclusion)
    {
        return;
    }

    const double now = glfwGetTime();

    if (m_ambientDirty && m_ambientBaking)
    {
        m_ambientBaking = false;
    }

    if (!m_ambientValid && !m_ambientBaking && !m_ambientDirty)
    {
        m_ambientDirty = true;
        m_ambientDirtyTime = now;
    }

    if (!m_ambientBaking && m_ambientDirty &&
        (!m_ambientValid || now - m_ambientDirtyTime >= AMBIENT_QUIET_SECONDS))
    {
        if (beginAmbientBake() && !m_ambientValid)
        {
            bakeAmbientBlocking();
            return;
        }
    }

    if (m_ambientBaking)
    {
        prepareAmbientStep(M_CURRENTFRAME);
    }
}

void BlackGlass::bakeAmbientBlocking()
{
    const auto start = std::chrono::steady_clock::now();

    vkDeviceWaitIdle(M_DEVICE);
    while (m_ambientBaking)
    {
        prepareAmbientStep(0);
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();
        recordAmbientStep(commandBuffer, 0);
        endSingleTimeCommands(commandBuffer);
    }

    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::cout << "  [bakeAmbientBlocking] Baked in " << ms << " ms.\n";
}

void BlackGlass::prepareAmbientStep(uint32_t slot)
{
    const uint32_t count = std::min(AMBIENT_DIRS_PER_STEP, AMBIENT_DIRECTIONS - m_ambientNextDir);

    const glm::vec3 size = glm::vec3(m_ambientBakeDims) * m_ambientBakeCell;
    const glm::vec3 center = m_ambientBakeMin + size * 0.5f;
    const float radius = std::max(0.5f * glm::length(size), 0.01f);
    const float texel = 2.0f * radius / static_cast<float>(AMBIENT_DEPTH_SIZE);
    const float biasWorld = texel * AMBIENT_BIAS_TEXELS + AMBIENT_BIAS_ABSOLUTE;
    const float biasNdc = biasWorld / (4.0f * radius);

    AmbientBakeParams& params = m_ambientParams;
    params = AmbientBakeParams{};

    for (uint32_t j = 0; j < count; j++)
    {
        glm::vec3 direction = ambientDirection(m_ambientNextDir + j);
        glm::vec3 up = std::abs(direction.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f)
                                                      : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::mat4 view = glm::lookAt(center + direction * (2.0f * radius), center, up);
        glm::mat4 proj = glm::ortho(-radius, radius, -radius, radius, 0.0f, 4.0f * radius);
        params.viewProj[j] = proj * view;
        params.direction[j] = glm::vec4(direction, biasNdc);
    }

    params.volumeMin = glm::vec4(m_ambientBakeMin, m_ambientBakeCell);
    params.dims = glm::uvec4(m_ambientBakeDims, count);
    params.invAxisWeight[0] = glm::vec4(m_ambientInvWeight[0], m_ambientInvWeight[1],
                                        m_ambientInvWeight[2], m_ambientInvWeight[3]);
    params.invAxisWeight[1] = glm::vec4(m_ambientInvWeight[4], m_ambientInvWeight[5], 0.0f, 0.0f);

    std::memcpy(M_AMBIENTBAKEUBO[slot].mapped, &params, sizeof(AmbientBakeParams));

    m_ambientStep.active = true;
    m_ambientStep.first = m_ambientNextDir == 0;
    m_ambientStep.last = m_ambientNextDir + count >= AMBIENT_DIRECTIONS;
    m_ambientStep.count = count;
    m_ambientStep.target = m_ambientBack;

    m_ambientNextDir += count;

    if (m_ambientStep.last)
    {
        m_ambientLive = m_ambientBack;
        m_ambientBack = 1 - m_ambientLive;
        m_ambientMin = m_ambientBakeMin;
        m_ambientDims = m_ambientBakeDims;
        m_ambientCell = m_ambientBakeCell;
        m_ambientValid = true;
        m_ambientBaking = false;
    }
}

void BlackGlass::recordAmbientStep(VkCommandBuffer commandBuffer, uint32_t slot)
{
    if (!m_ambientStep.active)
    {
        return;
    }

    const AmbientStep step = m_ambientStep;
    m_ambientStep.active = false;

    const uint32_t groupsX = (m_ambientBakeDims.x + 3) / 4;
    const uint32_t groupsY = (m_ambientBakeDims.y + 3) / 4;
    const uint32_t groupsZ = (m_ambientBakeDims.z + 3) / 4;

    VkDescriptorBufferInfo uboInfo{ M_AMBIENTBAKEUBO[slot].buffer, 0, sizeof(AmbientBakeParams) };
    VkDescriptorBufferInfo scratchInfo{ M_AMBIENTSCRATCH.buffer, 0, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo packedAInfo{ M_AMBIENTPACKEDA.buffer, 0, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo packedBInfo{ M_AMBIENTPACKEDB.buffer, 0, VK_WHOLE_SIZE };

    VkDescriptorImageInfo depthInfo{};
    depthInfo.sampler = M_SHADOWSAMPLER;
    depthInfo.imageView = M_AMBIENTDEPTH.view;
    depthInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    if (step.first)
    {
        globalBarrier(commandBuffer,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        vkCmdFillBuffer(commandBuffer, M_AMBIENTSCRATCH.buffer, 0, VK_WHOLE_SIZE, 0);
        globalBarrier(commandBuffer,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    }

    for (uint32_t j = 0; j < step.count; j++)
    {
        depthLayerBarrier(commandBuffer, M_AMBIENTDEPTH.image, j,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_NONE,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

        VkRenderingAttachmentInfo depthAttachment{};
        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.imageView = M_AMBIENTDEPTHVIEWS[j];
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

        VkRenderingInfo passInfo{};
        passInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        passInfo.renderArea.extent = { AMBIENT_DEPTH_SIZE, AMBIENT_DEPTH_SIZE };
        passInfo.layerCount = 1;
        passInfo.pDepthAttachment = &depthAttachment;

        vkCmdBeginRendering(commandBuffer, &passInfo);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, M_SHADOWPIPELINE);

        VkViewport viewport{};
        viewport.width = static_cast<float>(AMBIENT_DEPTH_SIZE);
        viewport.height = static_cast<float>(AMBIENT_DEPTH_SIZE);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.extent = { AMBIENT_DEPTH_SIZE, AMBIENT_DEPTH_SIZE };
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        drawShadowCasters(commandBuffer, m_ambientParams.viewProj[j], false, glm::vec3(0.0f), 0.0f);

        vkCmdEndRendering(commandBuffer);

        depthLayerBarrier(commandBuffer, M_AMBIENTDEPTH.image, j,
            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    }

    {
        VkWriteDescriptorSet writes[3]{};
        writes[0] = bufferWrite(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &uboInfo);
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &depthInfo;
        writes[2] = bufferWrite(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &scratchInfo);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, M_AMBIENTACCUMPIPELINE);
        M_PUSHDESCRIPTORFN(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, M_AMBIENTPIPELINELAYOUT, 0, 3, writes);
        vkCmdDispatch(commandBuffer, groupsX, groupsY, groupsZ);
    }

    globalBarrier(commandBuffer,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

    if (!step.last)
    {
        return;
    }

    globalBarrier(commandBuffer,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

    {
        VkWriteDescriptorSet writes[4]{};
        writes[0] = bufferWrite(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &uboInfo);
        writes[1] = bufferWrite(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &scratchInfo);
        writes[2] = bufferWrite(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &packedAInfo);
        writes[3] = bufferWrite(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &packedBInfo);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, M_AMBIENTFINALIZEPIPELINE);
        M_PUSHDESCRIPTORFN(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, M_AMBIENTPIPELINELAYOUT, 0, 4, writes);
        vkCmdDispatch(commandBuffer, groupsX, groupsY, groupsZ);
    }

    globalBarrier(commandBuffer,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);

    AmbientVolume& target = m_ambientVolumes[step.target];

    colorBarrier(commandBuffer, target.imageA.image,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    colorBarrier(commandBuffer, target.imageB.image,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { m_ambientBakeDims.x, m_ambientBakeDims.y, m_ambientBakeDims.z };

    vkCmdCopyBufferToImage(commandBuffer, M_AMBIENTPACKEDA.buffer, target.imageA.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    vkCmdCopyBufferToImage(commandBuffer, M_AMBIENTPACKEDB.buffer, target.imageB.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    colorBarrier(commandBuffer, target.imageA.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    colorBarrier(commandBuffer, target.imageB.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

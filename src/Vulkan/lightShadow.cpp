#include "../BlackGlass.h"
#include "barrier.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

#include <glm/gtc/matrix_transform.hpp>

namespace
{

constexpr float LIGHT_NEAR_PLANE = 0.05f;
constexpr float POINT_FACE_FOV_DEGREES = 92.0f;
constexpr float SPOT_MAX_FOV_DEGREES = 170.0f;
constexpr float NORMAL_OFFSET_TEXELS = 1.5f;
constexpr float HOLD_BIAS = 0.75f;
constexpr double REBAKE_INTERVAL = 0.08;

struct LightShadowPush
{
    glm::mat4 model;
    glm::mat4 lightViewProj;
};

glm::vec3 centerOf(const Model& model)
{
    return (model.worldMin + model.worldMax) * 0.5f;
}

bool sphereTouchesBox(const glm::vec3& center, float radius, const glm::vec3& boxMin, const glm::vec3& boxMax)
{
    glm::vec3 closest = glm::clamp(center, boxMin, boxMax);
    glm::vec3 delta = closest - center;
    return glm::dot(delta, delta) <= radius * radius;
}

LightShadowState describeLight(const Model& model)
{
    const LightDef& def = *model.light;

    LightShadowState state;
    state.model = &model;
    state.spot = def.spot;
    state.position = centerOf(model);
    state.radius = std::max(def.radius, 0.1f);

    if (def.spot)
    {
        float yaw = glm::radians(model.yawDegrees);
        float pitch = glm::radians(def.pitch);
        state.direction = glm::normalize(glm::vec3(std::sin(yaw) * std::cos(pitch),
                                                   std::sin(pitch),
                                                   std::cos(yaw) * std::cos(pitch)));
        state.outer = std::clamp(def.outerAngle, 1.0f, 89.0f);

        float fov = std::min(2.0f * state.outer, SPOT_MAX_FOV_DEGREES);
        glm::vec3 up = std::abs(state.direction.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f)
                                                            : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::mat4 view = glm::lookAt(state.position, state.position + state.direction, up);
        glm::mat4 proj = glm::perspective(glm::radians(fov), 1.0f, LIGHT_NEAR_PLANE, state.radius);
        state.viewProj[0] = proj * view;
        state.normalOffset = 2.0f * std::tan(glm::radians(fov) * 0.5f) /
                             static_cast<float>(LIGHT_SHADOW_SIZE) * NORMAL_OFFSET_TEXELS;
    }
    else
    {
        const glm::vec3 directions[LIGHT_SHADOW_FACES] = {
            { 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f },
            { 0.0f, 1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f },
            { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f }
        };
        const glm::vec3 ups[LIGHT_SHADOW_FACES] = {
            { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f },
            { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f },
            { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }
        };

        glm::mat4 proj = glm::perspective(glm::radians(POINT_FACE_FOV_DEGREES), 1.0f,
                                          LIGHT_NEAR_PLANE, state.radius);
        for (uint32_t face = 0; face < LIGHT_SHADOW_FACES; face++)
        {
            glm::mat4 view = glm::lookAt(state.position, state.position + directions[face], ups[face]);
            state.viewProj[face] = proj * view;
        }
        state.normalOffset = 2.0f * std::tan(glm::radians(POINT_FACE_FOV_DEGREES) * 0.5f) /
                             static_cast<float>(LIGHT_SHADOW_SIZE) * NORMAL_OFFSET_TEXELS;
    }

    return state;
}

}

void BlackGlass::createLightShadowMap()
{
    std::cout << "  [createLightShadowMap] Starting ... \n";

    M_LIGHTSHADOWMAP = createImage(
        LIGHT_SHADOW_SIZE, LIGHT_SHADOW_SIZE, SHADOW_FORMAT, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_DEPTH_BIT,
        1, LIGHT_SHADOW_LAYERS, 0, VK_IMAGE_VIEW_TYPE_2D_ARRAY);

    for (uint32_t layer = 0; layer < LIGHT_SHADOW_LAYERS; layer++)
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = M_LIGHTSHADOWMAP.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = SHADOW_FORMAT;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = layer;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(M_DEVICE, &viewInfo, nullptr, &M_LIGHTSHADOWLAYERVIEWS[layer]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create light shadow layer view!");
        }
    }

    std::vector<LightShadowJob> clearJobs;
    clearJobs.reserve(LIGHT_SHADOW_LAYERS);
    for (uint32_t layer = 0; layer < LIGHT_SHADOW_LAYERS; layer++)
    {
        LightShadowJob job;
        job.layer = layer;
        clearJobs.push_back(job);
    }
    renderLightShadowLayers(clearJobs);

    m_lightShadows = {};
    m_lightShadowsStale = true;
    m_lightShadowNextBake = 0.0;
}

void BlackGlass::destroyLightShadowMap()
{
    for (VkImageView& view : M_LIGHTSHADOWLAYERVIEWS)
    {
        if (view != VK_NULL_HANDLE)
        {
            vkDestroyImageView(M_DEVICE, view, nullptr);
            view = VK_NULL_HANDLE;
        }
    }
    destroyImage(M_LIGHTSHADOWMAP);
}

int BlackGlass::lightShadowSlot(const Model* model) const
{
    for (uint32_t slot = 0; slot < LIGHT_SHADOW_MAX; slot++)
    {
        if (m_lightShadows[slot].baked && m_lightShadows[slot].model == model)
        {
            return static_cast<int>(slot);
        }
    }
    return -1;
}

void BlackGlass::updateLightShadows()
{
    if (M_LIGHTSHADOWMAP.image == VK_NULL_HANDLE)
    {
        return;
    }

    struct Candidate
    {
        const Model* model;
        float score;
    };

    std::vector<Candidate> candidates;
    for (const auto& model : M_MODELS)
    {
        if (!model->light || model->ghost || model->hidden || !model->light->enabled)
        {
            continue;
        }

        float distance = glm::distance(centerOf(*model), m_camera.position);
        bool held = false;
        for (const LightShadowState& slot : m_lightShadows)
        {
            if (slot.model == model.get())
            {
                held = true;
            }
        }
        candidates.push_back({ model.get(), held ? distance * HOLD_BIAS : distance });
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.score < b.score; });

    const size_t wanted = std::min<size_t>(candidates.size(), LIGHT_SHADOW_MAX);

    for (LightShadowState& slot : m_lightShadows)
    {
        bool keep = false;
        for (size_t i = 0; i < wanted; i++)
        {
            if (slot.model == candidates[i].model)
            {
                keep = true;
            }
        }
        if (!keep)
        {
            slot.model = nullptr;
            slot.baked = false;
        }
    }

    const double now = glfwGetTime();
    const bool allowRetune = now >= m_lightShadowNextBake;
    std::vector<uint32_t> bakeSlots;

    for (size_t i = 0; i < wanted; i++)
    {
        const Model* model = candidates[i].model;

        int index = -1;
        for (uint32_t slot = 0; slot < LIGHT_SHADOW_MAX; slot++)
        {
            if (m_lightShadows[slot].model == model)
            {
                index = static_cast<int>(slot);
            }
        }

        LightShadowState desired = describeLight(*model);

        if (index < 0)
        {
            for (uint32_t slot = 0; slot < LIGHT_SHADOW_MAX; slot++)
            {
                if (m_lightShadows[slot].model == nullptr)
                {
                    index = static_cast<int>(slot);
                    break;
                }
            }
            if (index < 0)
            {
                continue;
            }
            m_lightShadows[static_cast<size_t>(index)] = desired;
            bakeSlots.push_back(static_cast<uint32_t>(index));
        }
        else
        {
            LightShadowState& slot = m_lightShadows[static_cast<size_t>(index)];
            bool stale = m_lightShadowsStale || !slot.baked;
            if (stale || (allowRetune && !slot.matches(desired)))
            {
                slot = desired;
                bakeSlots.push_back(static_cast<uint32_t>(index));
            }
        }
    }

    m_lightShadowsStale = false;

    if (bakeSlots.empty())
    {
        return;
    }

    vkDeviceWaitIdle(M_DEVICE);

    std::vector<LightShadowJob> jobs;
    for (uint32_t slotIndex : bakeSlots)
    {
        const LightShadowState& slot = m_lightShadows[slotIndex];
        const uint32_t faces = slot.spot ? 1u : LIGHT_SHADOW_FACES;
        for (uint32_t face = 0; face < faces; face++)
        {
            LightShadowJob job;
            job.layer = slotIndex * LIGHT_SHADOW_FACES + face;
            job.viewProj = slot.viewProj[face];
            job.draw = true;
            job.center = slot.position;
            job.radius = slot.radius;
            jobs.push_back(job);
        }
    }

    renderLightShadowLayers(jobs);

    for (uint32_t slotIndex : bakeSlots)
    {
        m_lightShadows[slotIndex].baked = true;
    }
    m_lightShadowNextBake = now + REBAKE_INTERVAL;
}

void BlackGlass::renderLightShadowLayers(const std::vector<LightShadowJob>& jobs)
{
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    for (const LightShadowJob& job : jobs)
    {
        depthLayerBarrier(commandBuffer, M_LIGHTSHADOWMAP.image, job.layer,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

        VkRenderingAttachmentInfo depthAttachment{};
        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.imageView = M_LIGHTSHADOWLAYERVIEWS[job.layer];
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

        VkRenderingInfo passInfo{};
        passInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        passInfo.renderArea.extent = { LIGHT_SHADOW_SIZE, LIGHT_SHADOW_SIZE };
        passInfo.layerCount = 1;
        passInfo.pDepthAttachment = &depthAttachment;

        vkCmdBeginRendering(commandBuffer, &passInfo);

        if (job.draw)
        {
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, M_SHADOWPIPELINE);

            VkViewport viewport{};
            viewport.width = static_cast<float>(LIGHT_SHADOW_SIZE);
            viewport.height = static_cast<float>(LIGHT_SHADOW_SIZE);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

            VkRect2D scissor{};
            scissor.extent = { LIGHT_SHADOW_SIZE, LIGHT_SHADOW_SIZE };
            vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

            drawShadowCasters(commandBuffer, job.viewProj, true, job.center, job.radius);
        }

        vkCmdEndRendering(commandBuffer);

        depthLayerBarrier(commandBuffer, M_LIGHTSHADOWMAP.image, job.layer,
            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    }

    endSingleTimeCommands(commandBuffer);
}

void BlackGlass::drawShadowCasters(VkCommandBuffer commandBuffer, const glm::mat4& viewProj,
                                   bool limitToSphere, const glm::vec3& center, float radius)
{
    for (const auto& model : M_MODELS)
    {
        if (!model->visible || model->ghost || model->light || model->spawn)
        {
            continue;
        }
        if (limitToSphere && !sphereTouchesBox(center, radius, model->worldMin, model->worldMax))
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

            LightShadowPush push{ model->transform * prim.worldMatrix, viewProj };
            vkCmdPushConstants(commandBuffer, M_SHADOWPIPELINELAYOUT, VK_SHADER_STAGE_VERTEX_BIT,
                                0, sizeof(LightShadowPush), &push);
            vkCmdDrawIndexed(commandBuffer, prim.indexCount, 1, prim.firstIndex, 0, 0);
        }
    }
}

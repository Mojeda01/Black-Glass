#include "../BlackGlass.h"
#include <iostream>
#include <stdexcept>
#include <array>
#include "../imgui/imgui.h"
#include "../imgui/imgui_impl_vulkan.h"
#include <vector>
#include <algorithm>
#include <utility>

namespace
{

void transitionImageLayout(
    VkCommandBuffer commandBuffer, VkImage image,
    VkImageLayout oldLayout, VkImageLayout newLayout,
    VkAccessFlags2 srcAccessMask, VkAccessFlags2 dstAccessMask,
    VkPipelineStageFlags2 srcStageMask, VkPipelineStageFlags2 dstStageMask,
    VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT
)
{
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = srcStageMask;
    barrier.srcAccessMask = srcAccessMask;
    barrier.dstStageMask = dstStageMask;
    barrier.dstAccessMask = dstAccessMask;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspectMask; 
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkDependencyInfo dependencyInfo{};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.imageMemoryBarrierCount = 1;
    dependencyInfo.pImageMemoryBarriers = &barrier;

    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
}

void barrierRange(
    VkCommandBuffer commandBuffer, VkImage image,
    VkImageLayout oldLayout, VkImageLayout newLayout,
    VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
    VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
    uint32_t baseMip, uint32_t levelCount
)
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
    barrier.subresourceRange.layerCount = 1;

    VkDependencyInfo dependencyInfo{};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.imageMemoryBarrierCount = 1;
    dependencyInfo.pImageMemoryBarriers = &barrier;

    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
}

}   // namespace

void BlackGlass::recordCommandBuffers(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to begin recording command buffer.");
    }

    if (m_timestampsSupported && m_gpuTimingsEnabled)
    {
        vkCmdResetQueryPool(commandBuffer, M_TIMESTAMPPOOL, M_CURRENTFRAME * GPU_TIMESTAMPS, GPU_TIMESTAMPS);
        m_timestampRecorded[M_CURRENTFRAME] = true;
    }
    writeTimestamp(commandBuffer, 0);

    const bool msaa = M_MSAASAMPLES != VK_SAMPLE_COUNT_1_BIT;
    const bool bloom = m_settings.bloom && !M_BLOOMIMAGES.empty() && m_debugView == 0;

    struct TransparentItem
    {
        float distance;
        const Model* model;
        const Primitive* prim;
    };

    std::vector<const Model*> drawModels;
    for (const auto& model : M_MODELS)
    {
        if (model->visible)
        {
            drawModels.push_back(model.get());
        }
    }
    if (M_GROUND)
    {
        drawModels.push_back(M_GROUND.get());
    }

    std::vector<TransparentItem> transparentOrder;
    for (const Model* model : drawModels)
    {
        for (const Primitive& prim : model->asset->primitives)
        {
            if (prim.renderClass != RenderClass::Opaque)
            {
                glm::vec3 center = glm::vec3(model->transform * glm::vec4(prim.boundsCenter, 1.0f));
                transparentOrder.push_back({ glm::length(center - m_camera.position), model, &prim });
            }
        }
    }
    std::sort(transparentOrder.begin(), transparentOrder.end(),
                [](const TransparentItem& a, const TransparentItem& b) { return a.distance > b.distance; });
    const bool hasTransparent = !transparentOrder.empty();

    const Model* boundModel = nullptr;

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = M_UNIFORMBUFFERS[M_CURRENTFRAME].buffer;
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(UniformBufferObject);

    std::array<VkDescriptorImageInfo, IBL_TEXTURE_BINDINGS> iblInfos{};
    const VkImageView iblViews[IBL_TEXTURE_BINDINGS] = {
        M_IBLIRRADIANCE.view, M_IBLPREFILTER.view, M_IBLBRDF.view, M_IBLENV.view
    };
    for (uint32_t i = 0; i < IBL_TEXTURE_BINDINGS; i++)
    {
        iblInfos[i].sampler = M_IBLSAMPLER;
        iblInfos[i].imageView = iblViews[i];
        iblInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    VkDescriptorImageInfo shadowInfo{};
    shadowInfo.sampler = M_SHADOWSAMPLER;
    shadowInfo.imageView = M_SHADOWMAP.view;
    shadowInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo aoInfo{};
    aoInfo.sampler = M_POSTSAMPLER;
    aoInfo.imageView = M_AOBLUR.view;
    aoInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo reflInfo{};
    reflInfo.sampler = M_POSTSAMPLER;
    reflInfo.imageView = M_REFLCOLOR.view;
    reflInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo sceneInfo{};
    sceneInfo.sampler = M_SCENESAMPLER;
    sceneInfo.imageView = M_SCENECOPY.view;
    sceneInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    auto drawPrimitive = [&](const Model& model, const Primitive& prim)
    {
        if (boundModel != &model)
        {
            VkBuffer vertexBuffers[] = { model.asset->vertexBuffer.buffer };
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(commandBuffer, model.asset->indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
            boundModel = &model;
        }

        const Material& material = model.asset->materials[prim.materialIndex];

        VkDescriptorBufferInfo materialInfo{};
        materialInfo.buffer = model.asset->materialBuffer.buffer;
        materialInfo.offset = model.asset->materialStride * static_cast<VkDeviceSize>(prim.materialIndex);
        materialInfo.range = sizeof(MaterialParams);

        std::array<VkDescriptorImageInfo, MATERIAL_TEXTURE_SLOTS> imageInfos{};
        for (uint32_t slot = 0; slot < MATERIAL_TEXTURE_SLOTS; slot++)
        {
            int textureIndex = material.textures[slot];
            bool present = textureIndex >= 0 && model.asset->textures[textureIndex].view != VK_NULL_HANDLE;

            imageInfos[slot].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfos[slot].imageView = present ? model.asset->textures[textureIndex].view
                                                  : M_FALLBACKTEXTURE.view;
            imageInfos[slot].sampler = present ? model.asset->textures[textureIndex].sampler
                                                : M_TEXTURESAMPLER;
        }

        std::array<VkWriteDescriptorSet, GRAPHICS_BINDING_COUNT> descriptorWrites{};

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[1].pBufferInfo = &materialInfo;

        for (uint32_t slot = 0; slot < MATERIAL_TEXTURE_SLOTS; slot++)
        {
            descriptorWrites[2 + slot].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[2 + slot].dstBinding = 2 + slot;
            descriptorWrites[2 + slot].descriptorCount = 1;
            descriptorWrites[2 + slot].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[2 + slot].pImageInfo = &imageInfos[slot];
        }

        for (uint32_t i = 0; i < IBL_TEXTURE_BINDINGS; i++)
        {
            uint32_t index = 2 + MATERIAL_TEXTURE_SLOTS + i;
            descriptorWrites[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[index].dstBinding = index;
            descriptorWrites[index].descriptorCount = 1;
            descriptorWrites[index].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[index].pImageInfo = &iblInfos[i];
        }

        descriptorWrites[SHADOW_BINDING_INDEX].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[SHADOW_BINDING_INDEX].dstBinding = SHADOW_BINDING_INDEX;
        descriptorWrites[SHADOW_BINDING_INDEX].descriptorCount = 1;
        descriptorWrites[SHADOW_BINDING_INDEX].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[SHADOW_BINDING_INDEX].pImageInfo = &shadowInfo;

        descriptorWrites[SCENE_COLOR_BINDING_INDEX].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[SCENE_COLOR_BINDING_INDEX].dstBinding = SCENE_COLOR_BINDING_INDEX;
        descriptorWrites[SCENE_COLOR_BINDING_INDEX].descriptorCount = 1;
        descriptorWrites[SCENE_COLOR_BINDING_INDEX].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[SCENE_COLOR_BINDING_INDEX].pImageInfo = &sceneInfo;

        descriptorWrites[AO_BINDING_INDEX].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[AO_BINDING_INDEX].dstBinding = AO_BINDING_INDEX;
        descriptorWrites[AO_BINDING_INDEX].descriptorCount = 1;
        descriptorWrites[AO_BINDING_INDEX].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[AO_BINDING_INDEX].pImageInfo = &aoInfo;

        descriptorWrites[REFLECTION_BINDING_INDEX].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[REFLECTION_BINDING_INDEX].dstBinding = REFLECTION_BINDING_INDEX;
        descriptorWrites[REFLECTION_BINDING_INDEX].descriptorCount = 1;
        descriptorWrites[REFLECTION_BINDING_INDEX].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[REFLECTION_BINDING_INDEX].pImageInfo = &reflInfo;

        M_PUSHDESCRIPTORFN(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            M_PIPELINELAYOUT, 0,
                            static_cast<uint32_t>(descriptorWrites.size()),
                            descriptorWrites.data());

        glm::mat4 world = model.transform * prim.worldMatrix;
        vkCmdPushConstants(commandBuffer, M_PIPELINELAYOUT, VK_SHADER_STAGE_VERTEX_BIT,
                            0, sizeof(glm::mat4), &world);

        glm::vec4 highlight(0.0f);
        if (model.ghost)
        {
            highlight = glm::vec4(0.25f, 0.85f, 1.0f, 0.18f);
        }
        else if (&model == m_selected)
        {
            highlight = glm::vec4(1.0f, 0.6f, 0.15f, 0.1f);
        }
        vkCmdPushConstants(commandBuffer, M_PIPELINELAYOUT, VK_SHADER_STAGE_FRAGMENT_BIT,
                            sizeof(glm::mat4), sizeof(glm::vec4), &highlight);
        vkCmdDrawIndexed(commandBuffer, prim.indexCount, 1, prim.firstIndex, 0, 0);
    };

    auto drawSky = [&]()
    {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, M_SKYPIPELINE);

        std::array<VkWriteDescriptorSet, 2> skyWrites{};
        skyWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        skyWrites[0].dstBinding = 0;
        skyWrites[0].descriptorCount = 1;
        skyWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        skyWrites[0].pBufferInfo = &bufferInfo;

        const uint32_t envIndex = 2 + MATERIAL_TEXTURE_SLOTS + 3;
        skyWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        skyWrites[1].dstBinding = envIndex;
        skyWrites[1].descriptorCount = 1;
        skyWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        skyWrites[1].pImageInfo = &iblInfos[3];

        M_PUSHDESCRIPTORFN(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            M_PIPELINELAYOUT, 0,
                            static_cast<uint32_t>(skyWrites.size()),
                            skyWrites.data());
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    };

    VkViewport fullViewport{};
    fullViewport.width = static_cast<float>(M_SWAPCHAINEXTENT.width);
    fullViewport.height = static_cast<float>(M_SWAPCHAINEXTENT.height);
    fullViewport.maxDepth = 1.0f;
    VkRect2D fullScissor{};
    fullScissor.extent = M_SWAPCHAINEXTENT;

    if (m_ssaoActive)
    {
        transitionImageLayout(
            commandBuffer, M_PREPASSDEPTH.image,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            VK_ACCESS_2_MEMORY_WRITE_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_IMAGE_ASPECT_DEPTH_BIT
        );

        VkRenderingAttachmentInfo prepassDepth{};
        prepassDepth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        prepassDepth.imageView = M_PREPASSDEPTH.view;
        prepassDepth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        prepassDepth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        prepassDepth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        prepassDepth.clearValue.depthStencil = { 1.0f, 0 };

        VkRenderingInfo prepassInfo{};
        prepassInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        prepassInfo.renderArea.extent = M_SWAPCHAINEXTENT;
        prepassInfo.layerCount = 1;
        prepassInfo.pDepthAttachment = &prepassDepth;

        vkCmdBeginRendering(commandBuffer, &prepassInfo);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, M_PREPASSPIPELINE);
        vkCmdSetViewport(commandBuffer, 0, 1, &fullViewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &fullScissor);

        VkWriteDescriptorSet frameWrite{};
        frameWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        frameWrite.dstBinding = 0;
        frameWrite.descriptorCount = 1;
        frameWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        frameWrite.pBufferInfo = &bufferInfo;
        M_PUSHDESCRIPTORFN(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, M_PIPELINELAYOUT, 0, 1, &frameWrite);

        const Model* prepassBound = nullptr;
        for (const Model* model : drawModels)
        {
            for (const Primitive& prim : model->asset->primitives)
            {
                if (prim.renderClass != RenderClass::Opaque ||
                    model->asset->materials[prim.materialIndex].params.flags1.y == 1)
                {
                    continue;
                }
                if (prepassBound != model)
                {
                    VkBuffer vertexBuffers[] = { model->asset->vertexBuffer.buffer };
                    VkDeviceSize offsets[] = { 0 };
                    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
                    vkCmdBindIndexBuffer(commandBuffer, model->asset->indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
                    prepassBound = model;
                }
                glm::mat4 world = model->transform * prim.worldMatrix;
                vkCmdPushConstants(commandBuffer, M_PIPELINELAYOUT, VK_SHADER_STAGE_VERTEX_BIT,
                                    0, sizeof(glm::mat4), &world);
                vkCmdDrawIndexed(commandBuffer, prim.indexCount, 1, prim.firstIndex, 0, 0);
            }
        }
        vkCmdEndRendering(commandBuffer);

        transitionImageLayout(
            commandBuffer, M_PREPASSDEPTH.image,
            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_IMAGE_ASPECT_DEPTH_BIT
        );

        auto fxPass = [&](VkPipeline pipeline, AllocatedImage& target, VkExtent2D extent, VkImageView inputA,
                          VkImageView inputB, const glm::vec4& params)
        {
            transitionImageLayout(
                commandBuffer, target.image,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_ACCESS_2_MEMORY_WRITE_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
            );

            VkRenderingAttachmentInfo attachment{};
            attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            attachment.imageView = target.view;
            attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

            VkRenderingInfo passInfo{};
            passInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            passInfo.renderArea.extent = extent;
            passInfo.layerCount = 1;
            passInfo.colorAttachmentCount = 1;
            passInfo.pColorAttachments = &attachment;

            VkViewport passViewport{};
            passViewport.width = static_cast<float>(extent.width);
            passViewport.height = static_cast<float>(extent.height);
            passViewport.maxDepth = 1.0f;
            VkRect2D passScissor{};
            passScissor.extent = extent;

            vkCmdBeginRendering(commandBuffer, &passInfo);
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdSetViewport(commandBuffer, 0, 1, &passViewport);
            vkCmdSetScissor(commandBuffer, 0, 1, &passScissor);

            VkDescriptorImageInfo infos[2]{};
            infos[0] = { M_POINTSAMPLER, inputA, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            infos[1] = { M_POINTSAMPLER, inputB, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

            VkWriteDescriptorSet writes[3]{};
            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstBinding = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[0].pBufferInfo = &bufferInfo;
            for (uint32_t i = 0; i < 2; i++)
            {
                writes[1 + i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[1 + i].dstBinding = 1 + i;
                writes[1 + i].descriptorCount = 1;
                writes[1 + i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[1 + i].pImageInfo = &infos[i];
            }
            M_PUSHDESCRIPTORFN(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, M_FXPIPELINELAYOUT, 0, 3, writes);
            vkCmdPushConstants(commandBuffer, M_FXPIPELINELAYOUT, VK_SHADER_STAGE_FRAGMENT_BIT,
                                0, sizeof(glm::vec4), &params);
            vkCmdDraw(commandBuffer, 3, 1, 0, 0);
            vkCmdEndRendering(commandBuffer);

            transitionImageLayout(
                commandBuffer, target.image,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
            );
        };

        VkExtent2D aoExtent{ std::max((M_SWAPCHAINEXTENT.width + 1) / 2, 1u),
                             std::max((M_SWAPCHAINEXTENT.height + 1) / 2, 1u) };
        fxPass(M_SSAOPIPELINE, M_AORAW, aoExtent, M_PREPASSDEPTH.view, M_PREPASSDEPTH.view,
                glm::vec4(m_ssaoWorldRadius, m_settings.ssaoIntensity, 0.0f, 0.0f));
        fxPass(M_SSAOBLURPIPELINE, M_AOBLUR, M_SWAPCHAINEXTENT, M_AORAW.view, M_PREPASSDEPTH.view,
                glm::vec4(0.0f, 0.0f, static_cast<float>(aoExtent.width), static_cast<float>(aoExtent.height)));
    }

    writeTimestamp(commandBuffer, 1);

    if (m_reflectionActive)
    {
        const VkExtent2D reflExtent = { std::max(M_SWAPCHAINEXTENT.width / 2, 1u),
                                        std::max(M_SWAPCHAINEXTENT.height / 2, 1u) };

        transitionImageLayout(
            commandBuffer, M_REFLDEPTH.image,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            VK_ACCESS_2_MEMORY_WRITE_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
            VK_IMAGE_ASPECT_DEPTH_BIT
        );
        transitionImageLayout(
            commandBuffer, M_REFLCOLOR.image,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_ACCESS_2_MEMORY_WRITE_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
        );
        if (msaa)
        {
            transitionImageLayout(
                commandBuffer, M_REFLMSAA.image,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_ACCESS_2_MEMORY_WRITE_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
            );
        }

        VkRenderingAttachmentInfo reflColor{};
        reflColor.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        reflColor.imageView = msaa ? M_REFLMSAA.view : M_REFLCOLOR.view;
        reflColor.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        reflColor.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        reflColor.clearValue = { { { 0.0f, 0.0f, 0.0f, 1.0f } } };
        if (msaa)
        {
            reflColor.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            reflColor.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
            reflColor.resolveImageView = M_REFLCOLOR.view;
            reflColor.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        else
        {
            reflColor.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        }

        VkRenderingAttachmentInfo reflDepth{};
        reflDepth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        reflDepth.imageView = M_REFLDEPTH.view;
        reflDepth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        reflDepth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        reflDepth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        reflDepth.clearValue.depthStencil = { 1.0f, 0 };

        VkRenderingInfo reflRendering{};
        reflRendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        reflRendering.renderArea.extent = reflExtent;
        reflRendering.layerCount = 1;
        reflRendering.colorAttachmentCount = 1;
        reflRendering.pColorAttachments = &reflColor;
        reflRendering.pDepthAttachment = &reflDepth;

        VkViewport reflViewport{};
        reflViewport.width = static_cast<float>(reflExtent.width);
        reflViewport.height = static_cast<float>(reflExtent.height);
        reflViewport.maxDepth = 1.0f;
        VkRect2D reflScissor{};
        reflScissor.extent = reflExtent;

        const VkBuffer frameBuffer = bufferInfo.buffer;
        const VkDescriptorImageInfo savedReflInfo = reflInfo;
        bufferInfo.buffer = M_REFLUNIFORMBUFFERS[M_CURRENTFRAME].buffer;
        reflInfo.sampler = M_TEXTURESAMPLER;
        reflInfo.imageView = M_FALLBACKTEXTURE.view;

        vkCmdBeginRendering(commandBuffer, &reflRendering);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, M_GRAPHICSPIPELINE);
        vkCmdSetViewport(commandBuffer, 0, 1, &reflViewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &reflScissor);
        boundModel = nullptr;
        for (const Model* model : drawModels)
        {
            if (model->isGround)
            {
                continue;
            }
            for (const Primitive& prim : model->asset->primitives)
            {
                if (prim.renderClass == RenderClass::Opaque)
                {
                    drawPrimitive(*model, prim);
                }
            }
        }
        drawSky();
        vkCmdEndRendering(commandBuffer);
        boundModel = nullptr;

        bufferInfo.buffer = frameBuffer;
        reflInfo = savedReflInfo;

        transitionImageLayout(
            commandBuffer, M_REFLCOLOR.image,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
        );
    }

    transitionImageLayout(
        commandBuffer, M_DEPTHIMAGE.image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_ACCESS_2_MEMORY_WRITE_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT
    );

    transitionImageLayout(
        commandBuffer, M_HDRCOLOR.image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_ACCESS_2_MEMORY_WRITE_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
    );

    if (msaa)
    {
        transitionImageLayout(
            commandBuffer, M_MSAACOLOR.image,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_ACCESS_2_MEMORY_WRITE_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
        );
    }

    VkClearValue clearColor = { { { 0.0f, 0.0f, 0.0f, 1.0f } } };

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = msaa ? M_MSAACOLOR.view : M_HDRCOLOR.view;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.clearValue = clearColor;
    if (msaa)
    {
        colorAttachment.storeOp = hasTransparent ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        colorAttachment.resolveImageView = M_HDRCOLOR.view;
        colorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    else
    {
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    }

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = M_DEPTHIMAGE.view;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = hasTransparent ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = { 0, 0 };
    renderingInfo.renderArea.extent = M_SWAPCHAINEXTENT;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = &depthAttachment;

    writeTimestamp(commandBuffer, 2);

    vkCmdBeginRendering(commandBuffer, &renderingInfo);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, M_GRAPHICSPIPELINE);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(M_SWAPCHAINEXTENT.width);
    viewport.height = static_cast<float>(M_SWAPCHAINEXTENT.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = M_SWAPCHAINEXTENT;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    for (const Model* model : drawModels)
    {
        for (const Primitive& prim : model->asset->primitives)
        {
            if (prim.renderClass == RenderClass::Opaque)
            {
                drawPrimitive(*model, prim);
            }
        }
    }

    drawSky();

    vkCmdEndRendering(commandBuffer);

    writeTimestamp(commandBuffer, 3);

    if (hasTransparent)
    {
        barrierRange(commandBuffer, M_HDRCOLOR.image,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
            0, 1);

        barrierRange(commandBuffer, M_SCENECOPY.image,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_NONE,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            0, M_SCENECOPYMIPS);

        VkImageCopy copyRegion{};
        copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.srcSubresource.layerCount = 1;
        copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.dstSubresource.layerCount = 1;
        copyRegion.extent = { M_SWAPCHAINEXTENT.width, M_SWAPCHAINEXTENT.height, 1 };
        vkCmdCopyImage(commandBuffer,
            M_HDRCOLOR.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            M_SCENECOPY.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &copyRegion);

        int32_t mipWidth = static_cast<int32_t>(M_SWAPCHAINEXTENT.width);
        int32_t mipHeight = static_cast<int32_t>(M_SWAPCHAINEXTENT.height);
        for (uint32_t level = 1; level < M_SCENECOPYMIPS; level++)
        {
            barrierRange(commandBuffer, M_SCENECOPY.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                level - 1, 1);

            int32_t nextWidth = std::max(mipWidth / 2, 1);
            int32_t nextHeight = std::max(mipHeight / 2, 1);

            VkImageBlit blit{};
            blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel = level - 1;
            blit.srcSubresource.layerCount = 1;
            blit.dstOffsets[1] = { nextWidth, nextHeight, 1 };
            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel = level;
            blit.dstSubresource.layerCount = 1;

            vkCmdBlitImage(commandBuffer,
                M_SCENECOPY.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                M_SCENECOPY.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &blit, VK_FILTER_LINEAR);

            mipWidth = nextWidth;
            mipHeight = nextHeight;
        }

        if (M_SCENECOPYMIPS > 1)
        {
            barrierRange(commandBuffer, M_SCENECOPY.image,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                0, M_SCENECOPYMIPS - 1);
        }
        barrierRange(commandBuffer, M_SCENECOPY.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            M_SCENECOPYMIPS - 1, 1);

        barrierRange(commandBuffer, M_HDRCOLOR.image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            0, 1);

        VkMemoryBarrier2 attachmentBarrier{};
        attachmentBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        attachmentBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT |
                                         VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        attachmentBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                                          VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        attachmentBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT |
                                         VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                         VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        attachmentBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                                          VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                                          VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                          VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkDependencyInfo attachmentDependency{};
        attachmentDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        attachmentDependency.memoryBarrierCount = 1;
        attachmentDependency.pMemoryBarriers = &attachmentBarrier;
        vkCmdPipelineBarrier2(commandBuffer, &attachmentDependency);

        VkRenderingAttachmentInfo glassColor{};
        glassColor.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        glassColor.imageView = msaa ? M_MSAACOLOR.view : M_HDRCOLOR.view;
        glassColor.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        glassColor.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        if (msaa)
        {
            glassColor.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            glassColor.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
            glassColor.resolveImageView = M_HDRCOLOR.view;
            glassColor.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        else
        {
            glassColor.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        }

        VkRenderingAttachmentInfo glassDepth{};
        glassDepth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        glassDepth.imageView = M_DEPTHIMAGE.view;
        glassDepth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        glassDepth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        glassDepth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

        VkRenderingInfo glassInfo{};
        glassInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        glassInfo.renderArea.extent = M_SWAPCHAINEXTENT;
        glassInfo.layerCount = 1;
        glassInfo.colorAttachmentCount = 1;
        glassInfo.pColorAttachments = &glassColor;
        glassInfo.pDepthAttachment = &glassDepth;

        vkCmdBeginRendering(commandBuffer, &glassInfo);
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
        boundModel = nullptr;

        RenderClass boundClass = RenderClass::Opaque;
        bool pipelineBound = false;
        for (const TransparentItem& entry : transparentOrder)
        {
            const Primitive& prim = *entry.prim;
            if (!pipelineBound || prim.renderClass != boundClass)
            {
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    prim.renderClass == RenderClass::Transmissive ? M_TRANSMISSIONPIPELINE : M_BLENDPIPELINE);
                boundClass = prim.renderClass;
                pipelineBound = true;
            }
            drawPrimitive(*entry.model, prim);
        }

        vkCmdEndRendering(commandBuffer);
    }

    transitionImageLayout(
        commandBuffer, M_HDRCOLOR.image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
    );

    writeTimestamp(commandBuffer, 4);

    auto fullscreenPass = [&](VkPipeline pipeline, VkImageView target, VkExtent2D extent,
                              VkAttachmentLoadOp loadOp, VkImageView input0, VkImageView input1,
                              const glm::vec4& params, bool endPass = true)
    {
        VkRenderingAttachmentInfo attachment{};
        attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        attachment.imageView = target;
        attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachment.loadOp = loadOp;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo passInfo{};
        passInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        passInfo.renderArea.offset = { 0, 0 };
        passInfo.renderArea.extent = extent;
        passInfo.layerCount = 1;
        passInfo.colorAttachmentCount = 1;
        passInfo.pColorAttachments = &attachment;

        vkCmdBeginRendering(commandBuffer, &passInfo);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        VkViewport passViewport{};
        passViewport.width = static_cast<float>(extent.width);
        passViewport.height = static_cast<float>(extent.height);
        passViewport.minDepth = 0.0f;
        passViewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &passViewport);

        VkRect2D passScissor{};
        passScissor.extent = extent;
        vkCmdSetScissor(commandBuffer, 0, 1, &passScissor);

        VkDescriptorImageInfo infos[2]{};
        VkWriteDescriptorSet writes[2]{};
        const VkImageView inputs[2] = { input0, input1 };
        for (uint32_t i = 0; i < 2; i++)
        {
            infos[i].sampler = M_POSTSAMPLER;
            infos[i].imageView = inputs[i];
            infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].pImageInfo = &infos[i];
        }
        M_PUSHDESCRIPTORFN(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            M_POSTPIPELINELAYOUT, 0, 2, writes);
        vkCmdPushConstants(commandBuffer, M_POSTPIPELINELAYOUT, VK_SHADER_STAGE_FRAGMENT_BIT,
                            0, sizeof(glm::vec4), &params);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        if (endPass)
        {
            vkCmdEndRendering(commandBuffer);
        }
    };

    auto bloomExtent = [&](size_t level)
    {
        return VkExtent2D{ M_SWAPCHAINEXTENT.width >> (level + 1), M_SWAPCHAINEXTENT.height >> (level + 1) };
    };

    if (bloom)
    {
        const size_t levels = M_BLOOMIMAGES.size();

        for (size_t level = 0; level < levels; level++)
        {
            transitionImageLayout(
                commandBuffer, M_BLOOMIMAGES[level].image,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_ACCESS_2_MEMORY_WRITE_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
            );

            VkImageView source = level == 0 ? M_HDRCOLOR.view : M_BLOOMIMAGES[level - 1].view;
            VkExtent2D sourceExtent = level == 0 ? M_SWAPCHAINEXTENT : bloomExtent(level - 1);

            fullscreenPass(M_BLOOMDOWNPIPELINE, M_BLOOMIMAGES[level].view, bloomExtent(level),
                            VK_ATTACHMENT_LOAD_OP_DONT_CARE, source, source,
                            glm::vec4(1.0f / sourceExtent.width, 1.0f / sourceExtent.height,
                                        level == 0 ? 1.0f : 0.0f, 0.0f));

            transitionImageLayout(
                commandBuffer, M_BLOOMIMAGES[level].image,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
            );
        }

        for (size_t level = levels - 1; level-- > 0;)
        {
            transitionImageLayout(
                commandBuffer, M_BLOOMIMAGES[level].image,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
            );

            VkImageView source = M_BLOOMIMAGES[level + 1].view;
            VkExtent2D sourceExtent = bloomExtent(level + 1);

            fullscreenPass(M_BLOOMUPPIPELINE, M_BLOOMIMAGES[level].view, bloomExtent(level),
                            VK_ATTACHMENT_LOAD_OP_LOAD, source, source,
                            glm::vec4(1.0f / sourceExtent.width, 1.0f / sourceExtent.height, 0.0f, 0.0f));

            transitionImageLayout(
                commandBuffer, M_BLOOMIMAGES[level].image,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
            );
        }
    }

    writeTimestamp(commandBuffer, 5);

    transitionImageLayout(
        commandBuffer, M_SWAPCHAINIMAGES[imageIndex],
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_ACCESS_2_NONE, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
    );

    fullscreenPass(M_COMPOSITEPIPELINE, M_SWAPCHAINIMAGEVIEWS[imageIndex], M_SWAPCHAINEXTENT,
                    VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                    M_HDRCOLOR.view, bloom ? M_BLOOMIMAGES[0].view : M_HDRCOLOR.view,
                    glm::vec4(m_settings.exposure, bloom ? m_settings.bloomStrength : 0.0f,
                                m_debugView != 0 ? 1.0f : 0.0f,
                                static_cast<float>(m_settings.tonemapper)), false);

    if (m_imguiReady && (m_showUi || m_quitPrompt))
    {
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
    }
    vkCmdEndRendering(commandBuffer);

    transitionImageLayout(
        commandBuffer, M_SWAPCHAINIMAGES[imageIndex],
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT
    );

    writeTimestamp(commandBuffer, 6);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to record command buffer!");
    }
}

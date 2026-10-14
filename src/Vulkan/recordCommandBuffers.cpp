#include "../AlethiaV3.h"
#include <iostream>
#include <stdexcept>
#include <array>

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

}   // namespace

void AlethiaV3::recordCommandBuffers(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to begin recording command buffer.");
    }

    const bool msaa = M_MSAASAMPLES != VK_SAMPLE_COUNT_1_BIT;
    const bool bloom = m_bloomEnabled && !M_BLOOMIMAGES.empty() && m_debugView == 0;

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
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
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
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = { 0, 0 };
    renderingInfo.renderArea.extent = M_SWAPCHAINEXTENT;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = &depthAttachment;

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

    VkBuffer vertexBuffers[] = { M_VERTEXBUFFER.buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, M_INDEXBUFFER.buffer, 0, VK_INDEX_TYPE_UINT32);

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

    for (const Primitive& prim : M_PRIMITIVES)
    {
        const Material& material = M_MATERIALS[prim.materialIndex];

        VkDescriptorBufferInfo materialInfo{};
        materialInfo.buffer = M_MATERIALBUFFER.buffer;
        materialInfo.offset = M_MATERIALSTRIDE * static_cast<VkDeviceSize>(prim.materialIndex);
        materialInfo.range = sizeof(MaterialParams);

        std::array<VkDescriptorImageInfo, MATERIAL_TEXTURE_SLOTS> imageInfos{};
        for (uint32_t slot = 0; slot < MATERIAL_TEXTURE_SLOTS; slot++)
        {
            int textureIndex = material.textures[slot];
            bool present = textureIndex >= 0 && M_TEXTURES[textureIndex].view != VK_NULL_HANDLE;

            imageInfos[slot].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfos[slot].imageView = present ? M_TEXTURES[textureIndex].view
                                                  : M_FALLBACKTEXTURE.view;
            imageInfos[slot].sampler = present ? M_TEXTURES[textureIndex].sampler
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

        M_PUSHDESCRIPTORFN(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            M_PIPELINELAYOUT, 0,
                            static_cast<uint32_t>(descriptorWrites.size()),
                            descriptorWrites.data());

        vkCmdPushConstants(commandBuffer, M_PIPELINELAYOUT, VK_SHADER_STAGE_VERTEX_BIT,
                            0, sizeof(glm::mat4), &prim.worldMatrix);
        vkCmdDrawIndexed(commandBuffer, prim.indexCount, 1, prim.firstIndex, 0, 0);
    }

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
    }

    vkCmdEndRendering(commandBuffer);

    transitionImageLayout(
        commandBuffer, M_HDRCOLOR.image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
    );

    auto fullscreenPass = [&](VkPipeline pipeline, VkImageView target, VkExtent2D extent,
                              VkAttachmentLoadOp loadOp, VkImageView input0, VkImageView input1,
                              const glm::vec4& params)
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
        vkCmdEndRendering(commandBuffer);
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

    transitionImageLayout(
        commandBuffer, M_SWAPCHAINIMAGES[imageIndex],
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_ACCESS_2_NONE, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
    );

    fullscreenPass(M_COMPOSITEPIPELINE, M_SWAPCHAINIMAGEVIEWS[imageIndex], M_SWAPCHAINEXTENT,
                    VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                    M_HDRCOLOR.view, bloom ? M_BLOOMIMAGES[0].view : M_HDRCOLOR.view,
                    glm::vec4(m_exposure, bloom ? m_bloomStrength : 0.0f,
                                m_debugView != 0 ? 1.0f : 0.0f, 0.0f));

    transitionImageLayout(
        commandBuffer, M_SWAPCHAINIMAGES[imageIndex],
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT
    );

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to record command buffer!");
    }
}

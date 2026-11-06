#include "../BlackGlass.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "../imgui/imgui.h"
#include "../imgui/imgui_impl_glfw.h"
#include "../imgui/imgui_impl_vulkan.h"
#include "../stb_image.h"

namespace
{
    constexpr int ATLAS_COLUMNS = 8;
    constexpr float DISPLAY_SCALE = 1.0f;
    constexpr double SPLASH_PUMP_INTERVAL = 0.025;
    constexpr double SPLASH_MAX_STEP = 0.05;
}

void BlackGlass::pumpSplash()
{
    glfwPollEvents();

    if (!m_splashActive || !m_imguiBackend || M_COMMANDBUFFERS.empty() || M_INFLIGHTFENCES.empty())
    {
        return;
    }
    if (glfwGetWindowAttrib(m_window, GLFW_ICONIFIED))
    {
        return;
    }

    const double now = glfwGetTime();
    if (m_splashPumpTime > 0.0 && now - m_splashPumpTime < SPLASH_PUMP_INTERVAL)
    {
        return;
    }
    m_splashPumpTime = now;

    vkWaitForFences(M_DEVICE, 1, &M_INFLIGHTFENCES[M_CURRENTFRAME], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    VkResult result = vkAcquireNextImageKHR(
        M_DEVICE, M_SWAPCHAIN, UINT64_MAX,
        M_IMAGEAVAILABLESEMAPHORES[M_CURRENTFRAME], VK_NULL_HANDLE, &imageIndex);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        return;
    }

    vkResetFences(M_DEVICE, 1, &M_INFLIGHTFENCES[M_CURRENTFRAME]);

    VkCommandBuffer commandBuffer = M_COMMANDBUFFERS[M_CURRENTFRAME];
    vkResetCommandBuffer(commandBuffer, 0);

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    drawSplash();
    ImGui::Render();

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    auto transition = [&](VkImageLayout oldLayout, VkImageLayout newLayout,
                          VkAccessFlags2 srcAccess, VkAccessFlags2 dstAccess,
                          VkPipelineStageFlags2 srcStage, VkPipelineStageFlags2 dstStage)
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
        barrier.image = M_SWAPCHAINIMAGES[imageIndex];
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;

        VkDependencyInfo dependencyInfo{};
        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.imageMemoryBarrierCount = 1;
        dependencyInfo.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
    };

    transition(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               VK_ACCESS_2_NONE, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
               VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

    VkRenderingAttachmentInfo attachment{};
    attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    attachment.imageView = M_SWAPCHAINIMAGEVIEWS[imageIndex];
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.clearValue.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = { 0, 0 };
    renderingInfo.renderArea.extent = M_SWAPCHAINEXTENT;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &attachment;

    vkCmdBeginRendering(commandBuffer, &renderingInfo);
    if (m_splashActive)
    {
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
    }
    vkCmdEndRendering(commandBuffer);

    transition(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
               VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_NONE,
               VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);

    vkEndCommandBuffer(commandBuffer);

    VkSemaphore waitSemaphores[] = { M_IMAGEAVAILABLESEMAPHORES[M_CURRENTFRAME] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSemaphore signalSemaphores[] = { M_RENDERFINISHEDSEMAPHORES[imageIndex] };

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(M_GRAPHICSQUEUE, 1, &submitInfo, M_INFLIGHTFENCES[M_CURRENTFRAME]) != VK_SUCCESS)
    {
        return;
    }

    VkSwapchainKHR swapchains[] = { M_SWAPCHAIN };

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;
    vkQueuePresentKHR(M_PRESENTQUEUE, &presentInfo);

    M_CURRENTFRAME = (M_CURRENTFRAME + 1) % MAX_FRAMES_IN_FLIGHT;
}

void BlackGlass::startSplash()
{
    std::string path = std::string(BLACK_GLASS_LOADING_DIR) + "/loading.gif";
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return;
    }
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.empty())
    {
        return;
    }

    int* delays = nullptr;
    int width = 0;
    int height = 0;
    int frames = 0;
    int components = 0;
    unsigned char* data = stbi_load_gif_from_memory(bytes.data(), static_cast<int>(bytes.size()), &delays,
                                                    &width, &height, &frames, &components, 4);
    if (!data || frames <= 0)
    {
        if (data)
        {
            stbi_image_free(data);
        }
        if (delays)
        {
            stbi_image_free(delays);
        }
        std::cerr << "  [splash] Could not decode " << path << "\n";
        return;
    }

    int columns = std::min(frames, ATLAS_COLUMNS);
    int rows = (frames + columns - 1) / columns;
    int atlasWidth = columns * width;
    int atlasHeight = rows * height;
    std::vector<unsigned char> atlas(static_cast<size_t>(atlasWidth) * atlasHeight * 4, 0);

    size_t frameBytes = static_cast<size_t>(width) * height * 4;
    for (int f = 0; f < frames; f++)
    {
        int column = f % columns;
        int row = f / columns;
        for (int y = 0; y < height; y++)
        {
            const unsigned char* source = data + f * frameBytes + static_cast<size_t>(y) * width * 4;
            unsigned char* target = atlas.data() +
                (static_cast<size_t>(row * height + y) * atlasWidth + static_cast<size_t>(column) * width) * 4;
            std::copy(source, source + static_cast<size_t>(width) * 4, target);
        }
    }

    m_splashEnds.clear();
    int elapsed = 0;
    for (int f = 0; f < frames; f++)
    {
        elapsed += std::max(delays ? delays[f] : 40, 10);
        m_splashEnds.push_back(elapsed);
    }

    stbi_image_free(data);
    if (delays)
    {
        stbi_image_free(delays);
    }

    try
    {
        m_splashImage = createTextureFromPixels(atlas.data(), atlasWidth, atlasHeight, VK_FORMAT_R8G8B8A8_SRGB);
    }
    catch (const std::exception& e)
    {
        std::cerr << "  [splash] Texture upload failed - " << e.what() << "\n";
        return;
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = 0.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    if (vkCreateSampler(M_DEVICE, &samplerInfo, nullptr, &m_splashSampler) != VK_SUCCESS)
    {
        destroyImage(m_splashImage);
        return;
    }

    m_splashSet = ImGui_ImplVulkan_AddTexture(m_splashSampler, m_splashImage.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    m_splashFrameSize = glm::ivec2(width, height);
    m_splashAtlasSize = glm::ivec2(atlasWidth, atlasHeight);
    m_splashColumns = columns;
    m_splashStart = -1.0;
    m_splashElapsed = 0.0;
    m_splashSkip = false;
    m_splashActive = true;
}

void BlackGlass::skipSplash()
{
    m_splashSkip = true;
}

void BlackGlass::releaseSplash()
{
    m_splashActive = false;
    if (m_splashSet == VK_NULL_HANDLE)
    {
        return;
    }
    vkDeviceWaitIdle(M_DEVICE);
    ImGui_ImplVulkan_RemoveTexture(m_splashSet);
    m_splashSet = VK_NULL_HANDLE;
    vkDestroySampler(M_DEVICE, m_splashSampler, nullptr);
    m_splashSampler = VK_NULL_HANDLE;
    destroyImage(m_splashImage);
    m_splashImage = AllocatedImage{};
}

void BlackGlass::drawSplash()
{
    const double now = glfwGetTime();
    if (m_splashStart < 0.0)
    {
        m_splashStart = now;
        m_splashElapsed = 0.0;
    }
    m_splashElapsed += std::min(now - m_splashStart, SPLASH_MAX_STEP);
    m_splashStart = now;

    int milliseconds = static_cast<int>(m_splashElapsed * 1000.0);
    if (m_loading && !m_splashSkip && !m_splashEnds.empty() && milliseconds >= m_splashEnds.back())
    {
        m_splashElapsed = 0.0;
        milliseconds = 0;
    }
    if (m_splashSkip || m_splashEnds.empty() || milliseconds >= m_splashEnds.back())
    {
        releaseSplash();
        return;
    }

    int frame = 0;
    while (frame + 1 < static_cast<int>(m_splashEnds.size()) && milliseconds >= m_splashEnds[static_cast<size_t>(frame)])
    {
        frame++;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImDrawList* list = ImGui::GetForegroundDrawList();
    ImVec2 position = viewport->Pos;
    ImVec2 size = viewport->Size;
    list->AddRectFilled(position, ImVec2(position.x + size.x, position.y + size.y), IM_COL32(0, 0, 0, 255));

    float width = static_cast<float>(m_splashFrameSize.x);
    float height = static_cast<float>(m_splashFrameSize.y);
    float pixelScale = DISPLAY_SCALE / std::max(ImGui::GetIO().DisplayFramebufferScale.x, 1.0f);
    float scale = std::min(pixelScale, std::min(size.x / width, size.y / height));
    ImVec2 imageSize(width * scale, height * scale);
    ImVec2 imageMin(position.x + (size.x - imageSize.x) * 0.5f, position.y + (size.y - imageSize.y) * 0.5f);

    int column = frame % m_splashColumns;
    int row = frame / m_splashColumns;
    float atlasWidth = static_cast<float>(m_splashAtlasSize.x);
    float atlasHeight = static_cast<float>(m_splashAtlasSize.y);
    ImVec2 uv0((column * width + 0.5f) / atlasWidth, (row * height + 0.5f) / atlasHeight);
    ImVec2 uv1(((column + 1) * width - 0.5f) / atlasWidth, ((row + 1) * height - 0.5f) / atlasHeight);

    list->AddImage((ImTextureID)(uintptr_t)m_splashSet, imageMin,
                   ImVec2(imageMin.x + imageSize.x, imageMin.y + imageSize.y), uv0, uv1);
}

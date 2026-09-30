#include "AlethiaV3.h"
#include <iostream>
#include <stdexcept>

// Constructor
AlethiaV3::AlethiaV3(GLFWwindow* window) 
    : m_window(window)
{
    std::cout << "[AlethiaV3] Constructor called\n"; 
}

// Destructor
AlethiaV3::~AlethiaV3()
{
    std::cout << "[AlethiaV3] Destructor called\n";
    cleanup();
}


void AlethiaV3::drawFrame()
{
    // wait for the GPU to finish with this frame-in-flight slot's resources
    // before reusing its command buffer.
    vkWaitForFences(M_DEVICE, 1, &M_INFLIGHTFENCES[M_CURRENTFRAME], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(
        M_DEVICE, M_SWAPCHAIN, UINT64_MAX,
        M_IMAGEAVAILABLESEMAPHORES[M_CURRENTFRAME], VK_NULL_HANDLE, &imageIndex
    );

    if(result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        // Swapchain recreation not implemented yet; nothing to do this frame.
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        throw std::runtime_error("Failed to acquire swapchain image!");
    }

    // Only reset the fence once we know we are actually submitting work,
    // so an early return above doesn't leave the fence permanently unsignaled.
    vkResetFences(M_DEVICE, 1, &M_INFLIGHTFENCES[M_CURRENTFRAME]);

    vkResetCommandBuffer(M_COMMANDBUFFERS[M_CURRENTFRAME], 0);
    recordCommandBuffers(M_COMMANDBUFFERS[M_CURRENTFRAME], imageIndex);

    VkSemaphore waitSemaphores[] = { M_IMAGEAVAILABLESEMAPHORES[M_CURRENTFRAME] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSemaphore signalSemaphores[] = { M_RENDERFINISHEDSEMAPHORES[imageIndex] }; 

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &M_COMMANDBUFFERS[M_CURRENTFRAME];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(M_GRAPHICSQUEUE, 1, &submitInfo, M_INFLIGHTFENCES[M_CURRENTFRAME]) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to submit draw command buffer!");
    }

    VkSwapchainKHR swapchains[] = { M_SWAPCHAIN };

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(M_PRESENTQUEUE, &presentInfo);

    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR && result != VK_ERROR_OUT_OF_DATE_KHR)
    {
        throw std::runtime_error("Failed to present swapchain image!");
    }

    M_CURRENTFRAME = (M_CURRENTFRAME + 1) % MAX_FRAMES_IN_FLIGHT;
}

void AlethiaV3::cleanup()
{
    std::cout << "  [cleanup] Starting ... \n";

    if (M_DEVICE != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(M_DEVICE);
    }

    for (auto ImageView : M_SWAPCHAINIMAGEVIEWS)
    {
        vkDestroyImageView(M_DEVICE, ImageView, nullptr);
    }
    if (!M_SWAPCHAINIMAGEVIEWS.empty())
    {
        std::cout << "  [cleanup] " << M_SWAPCHAINIMAGEVIEWS.size() << " image views destroyed. \n";
    }

    for (size_t i = 0; i < M_INFLIGHTFENCES.size(); i++)
    { 
        vkDestroySemaphore(M_DEVICE, M_IMAGEAVAILABLESEMAPHORES[i], nullptr);
        vkDestroyFence(M_DEVICE, M_INFLIGHTFENCES[i], nullptr);
    }
    for (size_t i = 0; i < M_RENDERFINISHEDSEMAPHORES.size(); i++)
    {
        vkDestroySemaphore(M_DEVICE, M_RENDERFINISHEDSEMAPHORES[i], nullptr);
    }
    if (!M_INFLIGHTFENCES.empty())
    {
        std::cout << "  [cleanup] Sync Objects destroyed. \n";
    }

    if (M_GRAPHICSPIPELINE != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(M_DEVICE, M_GRAPHICSPIPELINE, nullptr);
        std::cout << "  [cleanup] Graphics pipeline destroyed.\n";
    }

    if (M_PIPELINELAYOUT != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(M_DEVICE, M_PIPELINELAYOUT, nullptr);
        std::cout << "  [cleanup] Pipeline layout destroyed. \n";
    }

    if (M_COMMANDPOOL != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(M_DEVICE, M_COMMANDPOOL, nullptr);
        std::cout << "  [cleanup] Command pool destroyed. \n";
    }

    if (M_SWAPCHAIN != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(M_DEVICE, M_SWAPCHAIN, nullptr);
        std::cout << "  [cleanup] Swapchain destroyed. \n";
    }
    
    if (M_DEVICE != VK_NULL_HANDLE)
    {
        vkDestroyDevice(M_DEVICE, nullptr);
        std::cout << "  [cleanup] Logical device destroyed.\n";
    }

    if (M_SURFACE != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(M_INSTANCE, M_SURFACE, nullptr);
        std::cout << "  [cleanup] Surface destroyed.\n";
    }

    if (M_DEBUGMESSENGER != VK_NULL_HANDLE)
    {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(M_INSTANCE, "vkDestroyDebugUtilsMessengerEXT");
        if (func != nullptr)
        {
            func(M_INSTANCE, M_DEBUGMESSENGER, nullptr);
            std::cout << "  [cleanup] Debug messenger destroyed.\n";
        }
    }

    if (M_INSTANCE != VK_NULL_HANDLE)
    {
        vkDestroyInstance(M_INSTANCE, nullptr);
        std::cout << "  [cleanup] Instance destroyed.\n";
    }

    std::cout << " [cleanup] Complete.\n";
}
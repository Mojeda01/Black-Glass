#include "../BlackGlass.h"
#include <iostream>
#include <stdexcept>

void BlackGlass::cleanupSwapchain()
{
    destroyHdrResources();
    destroyEffectResources();
    destroyImage(M_DEPTHIMAGE);
    
    for (VkImageView imageView : M_SWAPCHAINIMAGEVIEWS)
    {
        vkDestroyImageView(M_DEVICE, imageView, nullptr);
    }
    M_SWAPCHAINIMAGEVIEWS.clear();
    
    for (VkSemaphore semaphore : M_RENDERFINISHEDSEMAPHORES)
    {
        vkDestroySemaphore(M_DEVICE, semaphore, nullptr);
    }
    M_RENDERFINISHEDSEMAPHORES.clear();

    if (M_SWAPCHAIN != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(M_DEVICE, M_SWAPCHAIN, nullptr);
        M_SWAPCHAIN = VK_NULL_HANDLE;
    }
}

void BlackGlass::recreateSwapchain()
{
    int width = 0, height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);
    while (width == 0 || height == 0)
    {
        glfwGetFramebufferSize(m_window, &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(M_DEVICE);

    cleanupSwapchain();

    createSwapchain();
    createImageViews();
    createDepthResources();
    createHdrResources();
    createEffectResources();

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    M_RENDERFINISHEDSEMAPHORES.resize(M_SWAPCHAINIMAGES.size());
    for (size_t i = 0; i < M_SWAPCHAINIMAGES.size(); i++)
    {
        if (vkCreateSemaphore(M_DEVICE, &semaphoreInfo, nullptr, &M_RENDERFINISHEDSEMAPHORES[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to recreate render-finished semaphore!");
        }
    }

    std::cout << "  [recreateSwapchain] Rebuilt at " << M_SWAPCHAINEXTENT.width
              << "x" << M_SWAPCHAINEXTENT.height << " with "
              << M_SWAPCHAINIMAGES.size() << " images.\n";
}

#include "BlackGlass.h"
#include <iostream>
#include <stdexcept>

// Constructor
BlackGlass::BlackGlass(GLFWwindow* window) 
    : m_window(window)
{
    std::cout << "[BlackGlass] Constructor called\n"; 
}

// Destructor
BlackGlass::~BlackGlass()
{
    std::cout << "[BlackGlass] Destructor called\n";
    cleanup();
}


void BlackGlass::drawFrame()
{
    // wait for the GPU to finish with this frame-in-flight slot's resources
    // before reusing its command buffer.
    vkWaitForFences(M_DEVICE, 1, &M_INFLIGHTFENCES[M_CURRENTFRAME], VK_TRUE, UINT64_MAX);
    collectGpuTimings(M_CURRENTFRAME);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(
        M_DEVICE, M_SWAPCHAIN, UINT64_MAX,
        M_IMAGEAVAILABLESEMAPHORES[M_CURRENTFRAME], VK_NULL_HANDLE, &imageIndex
    );

    if(result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        recreateSwapchain();
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        throw std::runtime_error("Failed to acquire swapchain image!");
    }

    // Only reset the fence once we know we are actually submitting work,
    // so an early return above doesn't leave the fence permanently unsignaled.
    vkResetFences(M_DEVICE, 1, &M_INFLIGHTFENCES[M_CURRENTFRAME]);

    vkResetCommandBuffer(M_COMMANDBUFFERS[M_CURRENTFRAME], 0);
    beginUiFrame();
    updateInteraction();
    if (m_shadowDirty)
    {
        vkDeviceWaitIdle(M_DEVICE);
        refreshScene();
        bakeShadowMap();
        m_shadowDirty = false;
    }
    updateUniformBuffer(M_CURRENTFRAME);
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

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_framebufferResized)
    {
        m_framebufferResized = false;
        recreateSwapchain();
    }
    else if (result != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to present swapchain image!");
    }

    M_CURRENTFRAME = (M_CURRENTFRAME + 1) % MAX_FRAMES_IN_FLIGHT;
}

void BlackGlass::cleanup()
{
    std::cout << "  [cleanup] Starting ... \n";

    if (M_DEVICE != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(M_DEVICE);
    }
    
    shutdownImGui();

    cleanupSwapchain();
    std::cout << "  [cleanup] Swapchain resources destroyed. \n";
    
    M_MODELS.clear();
    m_selected = nullptr;
    m_ghost = nullptr;
    for (auto& asset : M_LIBRARY)
    {
        destroyAsset(*asset);
    }
    M_LIBRARY.clear();
    if (M_GROUND)
    {
        destroyAsset(*M_GROUND->asset);
        M_GROUND.reset();
    }

    if (M_TEXTURESAMPLER != VK_NULL_HANDLE)
    {
        vkDestroySampler(M_DEVICE, M_TEXTURESAMPLER, nullptr);
    }
    destroyImage(M_FALLBACKTEXTURE);

    destroyImage(M_SHADOWMAP);
    if (M_SHADOWSAMPLER != VK_NULL_HANDLE)
    {
        vkDestroySampler(M_DEVICE, M_SHADOWSAMPLER, nullptr);
    }
    if (M_SHADOWPIPELINE != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(M_DEVICE, M_SHADOWPIPELINE, nullptr);
    }
    if (M_SHADOWPIPELINELAYOUT != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(M_DEVICE, M_SHADOWPIPELINELAYOUT, nullptr);
    }

    destroyImage(M_IBLENV);
    destroyImage(M_IBLIRRADIANCE);
    destroyImage(M_IBLPREFILTER);
    destroyImage(M_IBLBRDF);
    if (M_IBLSAMPLER != VK_NULL_HANDLE)
    {
        vkDestroySampler(M_DEVICE, M_IBLSAMPLER, nullptr);
    }
    if (M_SCENESAMPLER != VK_NULL_HANDLE)
    {
        vkDestroySampler(M_DEVICE, M_SCENESAMPLER, nullptr);
    }
    if (M_POSTSAMPLER != VK_NULL_HANDLE)
    {
        vkDestroySampler(M_DEVICE, M_POSTSAMPLER, nullptr);
    }

    for (auto& buffer : M_UNIFORMBUFFERS)
    {
        destroyBuffer(buffer);
    }
    for (auto& buffer : M_REFLUNIFORMBUFFERS)
    {
        destroyBuffer(buffer);
    }
    if (M_DESCRIPTORSETLAYOUT != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(M_DEVICE, M_DESCRIPTORSETLAYOUT, nullptr);
    }

    std::cout << "  [cleanup] Geometry buffers destroyed. \n";

    for (size_t i = 0; i < M_INFLIGHTFENCES.size(); i++)
    { 
        vkDestroySemaphore(M_DEVICE, M_IMAGEAVAILABLESEMAPHORES[i], nullptr);
        vkDestroyFence(M_DEVICE, M_INFLIGHTFENCES[i], nullptr);
    } 
    
    if (!M_INFLIGHTFENCES.empty())
    {
        std::cout << "  [cleanup] Sync Objects destroyed. \n";
    }

    if (M_TIMESTAMPPOOL != VK_NULL_HANDLE)
    {
        vkDestroyQueryPool(M_DEVICE, M_TIMESTAMPPOOL, nullptr);
        M_TIMESTAMPPOOL = VK_NULL_HANDLE;
    }

    for (VkPipeline pipeline : { M_PREPASSPIPELINE, M_SSAOPIPELINE, M_SSAOBLURPIPELINE, M_SKYPIPELINE, M_TRANSMISSIONPIPELINE, M_BLENDPIPELINE, M_BLOOMDOWNPIPELINE, M_BLOOMUPPIPELINE, M_COMPOSITEPIPELINE })
    {
        if (pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(M_DEVICE, pipeline, nullptr);
        }
    }

    if (M_POINTSAMPLER != VK_NULL_HANDLE)
    {
        vkDestroySampler(M_DEVICE, M_POINTSAMPLER, nullptr);
    }
    if (M_FXPIPELINELAYOUT != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(M_DEVICE, M_FXPIPELINELAYOUT, nullptr);
    }
    if (M_FXDESCRIPTORLAYOUT != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(M_DEVICE, M_FXDESCRIPTORLAYOUT, nullptr);
    }

    if (M_POSTPIPELINELAYOUT != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(M_DEVICE, M_POSTPIPELINELAYOUT, nullptr);
    }

    if (M_POSTDESCRIPTORLAYOUT != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(M_DEVICE, M_POSTDESCRIPTORLAYOUT, nullptr);
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
    
    destroyAllocator();

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

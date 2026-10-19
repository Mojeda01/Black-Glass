#include "BlackGlass.h"
#include "Editor/Editor.h"
#include "Debug/StateLog.h"
#include "Debug/DisplayTiming.h"
#include "Debug/CocoaProbe.h"
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
    FrameLog::close();
    shutdownCocoaProbe();
    StateLog::close();
}


FrameLogView BlackGlass::frameLogView() const
{
    FrameLogView view;
    view.extentW = M_SWAPCHAINEXTENT.width;
    view.extentH = M_SWAPCHAINEXTENT.height;
    glfwGetFramebufferSize(m_window, &view.fbW, &view.fbH);
    glfwGetWindowSize(m_window, &view.winW, &view.winH);
    switch (M_PRESENTMODE)
    {
        case VK_PRESENT_MODE_IMMEDIATE_KHR: view.presentMode = "IMMEDIATE"; break;
        case VK_PRESENT_MODE_MAILBOX_KHR: view.presentMode = "MAILBOX"; break;
        case VK_PRESENT_MODE_FIFO_KHR: view.presentMode = "FIFO"; break;
        case VK_PRESENT_MODE_FIFO_RELAXED_KHR: view.presentMode = "FIFO_RELAXED"; break;
        default: view.presentMode = "OTHER"; break;
    }
    view.imageCount = static_cast<uint32_t>(M_SWAPCHAINIMAGES.size());
    return view;
}

void BlackGlass::drawFrame()
{
    paceFrame();

    static double lastFrameStart = 0.0;
    const double frameStart = glfwGetTime();
    FrameLogSample sample;
    sample.time = frameStart;
    sample.frameMs = lastFrameStart > 0.0 ? (frameStart - lastFrameStart) * 1000.0 : 0.0;
    sample.frameSlot = M_CURRENTFRAME;
    lastFrameStart = frameStart;

    // wait for the GPU to finish with this frame-in-flight slot's resources
    // before reusing its command buffer.
    vkWaitForFences(M_DEVICE, 1, &M_INFLIGHTFENCES[M_CURRENTFRAME], VK_TRUE, UINT64_MAX);
    collectGpuTimings(M_CURRENTFRAME);
    const double fenceDone = glfwGetTime();
    sample.fenceMs = (fenceDone - frameStart) * 1000.0;

    uint32_t imageIndex = 0;
    VkResult result = vkAcquireNextImageKHR(
        M_DEVICE, M_SWAPCHAIN, UINT64_MAX,
        M_IMAGEAVAILABLESEMAPHORES[M_CURRENTFRAME], VK_NULL_HANDLE, &imageIndex
    );
    const double acquireDone = glfwGetTime();
    sample.acquireMs = (acquireDone - fenceDone) * 1000.0;
    sample.acquireResult = static_cast<int>(result);
    sample.imageIndex = imageIndex;

    if (result == VK_SUBOPTIMAL_KHR)
    {
        FrameLog::event("acquire_suboptimal", acquireDone, 0.0, frameLogView());
    }

    if(result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        FrameLog::event("acquire_out_of_date", acquireDone, 0.0, frameLogView());
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
    if (m_brushPurgePending)
    {
        purgeUnusedBrushAssets();
    }
    m_debugDraw.clear();
    if (m_editor)
    {
        m_editor->update(m_showUi);
    }
    if (m_shadowRefreshPending && glfwGetTime() >= m_shadowRefreshTime)
    {
        m_shadowRefreshPending = false;
        m_shadowDirty = true;
    }
    if (m_shadowDirty)
    {
        vkDeviceWaitIdle(M_DEVICE);
        refreshScene();
        bakeShadowMap();
        m_shadowDirty = false;
    }
    updateUniformBuffer(M_CURRENTFRAME);
    const double recordStart = glfwGetTime();
    recordCommandBuffers(M_COMMANDBUFFERS[M_CURRENTFRAME], imageIndex);
    const double recordDone = glfwGetTime();
    sample.updateMs = (recordStart - acquireDone) * 1000.0;
    sample.recordMs = (recordDone - recordStart) * 1000.0;

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

    const double cpuDone = glfwGetTime();
    sample.cpuMs = (cpuDone - acquireDone) * 1000.0;
    sample.submitMs = (cpuDone - recordDone) * 1000.0;

    VkSwapchainKHR swapchains[] = { M_SWAPCHAIN };

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(M_PRESENTQUEUE, &presentInfo);
    const double presentDone = glfwGetTime();
    sample.presentMs = (presentDone - cpuDone) * 1000.0;
    sample.presentResult = static_cast<int>(result);
    sample.presentAt = presentDone;
    sample.camX = m_camera.position.x;
    sample.camY = m_camera.position.y;
    sample.camZ = m_camera.position.z;
    sample.camYaw = m_camera.yaw;
    sample.camPitch = m_camera.pitch;
    CameraPose pose;
    pose.x = m_camera.position.x;
    pose.y = m_camera.position.y;
    pose.z = m_camera.position.z;
    pose.yaw = m_camera.yaw;
    pose.pitch = m_camera.pitch;
    sample.phase = DisplayTiming::notePresent(presentDone, pose);
    FrameLog::frame(sample, frameLogView());
    updateStateLog();

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_framebufferResized)
    {
        FrameLog::event(m_framebufferResized ? "present_resize_flag" : "present_out_of_date_or_suboptimal",
                        presentDone, 0.0, frameLogView());
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
    
    destroyViewModel();
    M_MODELS.clear();
    m_selected = nullptr;
    m_selection.clear();
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

    for (VkPipeline pipeline : { M_PREPASSPIPELINE, M_SSAOPIPELINE, M_SSAOBLURPIPELINE, M_SKYPIPELINE, M_TRANSMISSIONPIPELINE, M_BLENDPIPELINE, M_VIEWMODELPIPELINE, M_BLOOMDOWNPIPELINE, M_BLOOMUPPIPELINE, M_COMPOSITEPIPELINE, M_DEBUGLINEPIPELINE, M_DEBUGOVERLAYPIPELINE })
    {
        if (pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(M_DEVICE, pipeline, nullptr);
        }
    }

    for (AllocatedBuffer& buffer : M_DEBUGLINEBUFFERS)
    {
        destroyBuffer(buffer);
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

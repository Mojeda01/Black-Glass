#include "BlackGlass.h"
#include "Debug/StateLog.h"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <cstring>

void BlackGlass::initVulkan()
{
    std::cout << "[BlackGlass::initVulkan()]:: Started \n";
    m_loading = true;
    FrameLog::open();
    StateLog::open();

    std::cout << "→ createInstance()\n";
    createInstance();

    std::cout << "→ setupDebugMessenger() \n";
    setupDebugMessenger();

    std::cout << "→ createSurface()\n";
    createSurface();

    std::cout << "→ pickPhysicalDevice()\n";
    pickPhysicalDevice();

    std::cout << "→ createLogicalDevice()\n";
    createLogicalDevice();

    std::cout << "→ createAllocator()\n";
    createAllocator();
    createPipelineCache();

    std::cout << "→ createSwapchain()\n";
    createSwapchain();

    std::cout << "→ createImageViews()\n";
    createImageViews();
    
    std::cout << "→ createCommandPool()\n";
    createCommandPool();

    std::cout << "→ createCommandBuffers()\n";
    createCommandBuffers();

    std::cout << "→ createSyncObjects()\n";
    createSyncObjects();

    std::cout << "→ initImGui()\n";
    initImGui();
    startSplash();
    pumpSplash();

    std::cout << "→ pickMsaaSamples()\n";
    pickMsaaSamples();

    std::cout << "→ createDepthResources()\n";
    createDepthResources();

    std::cout << "→ createHdrResources()\n";
    createHdrResources();

    std::cout << "→ createEffectResources()\n";
    createEffectResources();
    pumpSplash();

    std::cout << "→ createTextureSampler()\n";
    createTextureSampler();
    createFallbackTexture();
    pumpSplash();

    std::cout << "→ createGroundModel()\n";
    createGroundModel();
    pumpSplash();

    std::cout << "→ importAsset()\n";
    int defaultAsset = importAsset(std::string(BLACK_GLASS_ASSET_DIR) + "/CarConcept.glb");
    if (defaultAsset < 0)
    {
        throw std::runtime_error("Failed to load the default model: " + m_loadError);
    }
    addInstance(defaultAsset, true);
    frameScene();
    pumpSplash();

    std::cout << "→ createDescriptorSetLayout()\n";
    createDescriptorSetLayout();
    pumpSplash();

    std::cout << "→ createIBL()\n";
    createIBL();
    pumpSplash();

    std::cout << "→ createUniformBuffers()\n";
    createUniformBuffers();

    createTimestampPool();

    std::cout << "→ createPipelineLayout()\n";
    createPipelineLayout();

    std::cout << "→ createGraphicsPipeline()\n";
    createGraphicsPipeline();
    pumpSplash();
    createDebugDraw();

    std::cout << "→ createPostResources()\n";
    createPostResources();
    pumpSplash();

    std::cout << "→ createEffectPipelines()\n";
    createEffectPipelines();
    pumpSplash();

    std::cout << "→ createShadowMap()\n";
    refreshScene();
    createShadowMap();
    pumpSplash();
    createLightShadowMap();
    pumpSplash();
    createAmbientVolume();
    pumpSplash();
    m_shadowDirty = false;

    std::cout << "→ loadViewModel()\n";
    loadViewModel();
    pumpSplash();

    updateAmbient();
    pumpSplash();

    savePipelineCache();
}

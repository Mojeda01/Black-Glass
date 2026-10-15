#include "BlackGlass.h"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <cstring>

void BlackGlass::initVulkan()
{
    std::cout << "[BlackGlass::initVulkan()]:: Started \n";

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

    std::cout << "→ createSwapchain()\n";
    createSwapchain();

    std::cout << "→ createImageViews()\n";
    createImageViews();
    
    std::cout << "→ createCommandPool()\n";
    createCommandPool();

    std::cout << "→ pickMsaaSamples()\n";
    pickMsaaSamples();

    std::cout << "→ createDepthResources()\n";
    createDepthResources();

    std::cout << "→ createHdrResources()\n";
    createHdrResources();

    std::cout << "→ createEffectResources()\n";
    createEffectResources();

    std::cout << "→ createTextureSampler()\n";
    createTextureSampler();
    createFallbackTexture();

    std::cout << "→ createGroundModel()\n";
    createGroundModel();

    std::cout << "→ importAsset()\n";
    int defaultAsset = importAsset(std::string(BLACK_GLASS_ASSET_DIR) + "/CarConcept.glb");
    if (defaultAsset < 0)
    {
        throw std::runtime_error("Failed to load the default model: " + m_loadError);
    }
    addInstance(defaultAsset, true);
    frameScene();

    std::cout << "→ createDescriptorSetLayout()\n";
    createDescriptorSetLayout();

    std::cout << "→ createIBL()\n";
    createIBL();

    std::cout << "→ createUniformBuffers()\n";
    createUniformBuffers();

    std::cout << "→ createCommandBuffers()\n";
    createCommandBuffers();

    std::cout << "→ createSyncObjects()\n";
    createSyncObjects();
    createTimestampPool();

    std::cout << "→ createPipelineLayout()\n";
    createPipelineLayout();

    std::cout << "→ createGraphicsPipeline()\n";
    createGraphicsPipeline();

    std::cout << "→ createPostResources()\n";
    createPostResources();

    std::cout << "→ createEffectPipelines()\n";
    createEffectPipelines();

    std::cout << "→ createShadowMap()\n";
    refreshScene();
    createShadowMap();
    m_shadowDirty = false;
}

#include "AlethiaV3.h"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <cstring>

void AlethiaV3::initVulkan()
{
    std::cout << "[AlethiaV3::initVulkan()]:: Started \n";

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

    std::cout << "→ createPipelineLayout()\n";
    createPipelineLayout();

    std::cout << "→ createGraphicsPipeline()\n";
    createGraphicsPipeline();
}

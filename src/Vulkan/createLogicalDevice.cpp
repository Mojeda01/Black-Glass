#include "../AlethiaV3.h"
#include <iostream>
#include <stdexcept>
#include <set>
#include <vector>

void AlethiaV3::createLogicalDevice()
{
    std::cout << "  [createLogicalDevice] Starting... \n";
    QueueFamilyIndices indices = findQueueFamilies(M_PHYSICALDEVICE);

    std::set<uint32_t> uniqueQueueFamilies = {
        indices.graphicsFamily.value(),
        indices.presentFamily.value()
    };

    float queuePriority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    queueCreateInfos.reserve(uniqueQueueFamilies.size());
    
    for (uint32_t queueFamily : uniqueQueueFamilies)
    {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    // feature chain
    
    VkPhysicalDeviceShaderDrawParameterFeatures shaderDrawParametersFeatures{};
    shaderDrawParametersFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES;
    shaderDrawParametersFeatures.shaderDrawParameters = VK_TRUE;

    VkPhysicalDeviceMultiviewFeatures multiviewFeatures{};
    multiviewFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES;
    multiviewFeatures.multiview = VK_TRUE;
    multiviewFeatures.pNext = &shaderDrawParametersFeatures;

    VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures{};
    bufferDeviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES; 
    bufferDeviceAddressFeatures.bufferDeviceAddress = VK_TRUE;
    bufferDeviceAddressFeatures.pNext = &multiviewFeatures;
    
    VkPhysicalDeviceSynchronization2Features synchronization2Features{};
    synchronization2Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
    synchronization2Features.synchronization2 = VK_TRUE;
    synchronization2Features.pNext = &bufferDeviceAddressFeatures;

    VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures{};
    dynamicRenderingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
    dynamicRenderingFeatures.dynamicRendering = VK_TRUE;
    dynamicRenderingFeatures.pNext = &synchronization2Features;

    // base Vulkan 1.0 feature set
    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;
    deviceFeatures.fillModeNonSolid = VK_TRUE;
    deviceFeatures.independentBlend = VK_TRUE;
    deviceFeatures.tessellationShader = VK_TRUE;
    deviceFeatures.shaderInt64 = VK_TRUE;

    // device creation
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &dynamicRenderingFeatures;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(DEVICE_EXTENSIONS.size());
    createInfo.ppEnabledExtensionNames = DEVICE_EXTENSIONS.data();

    createInfo.enabledLayerCount = static_cast<uint32_t>(VALIDATION_LAYERS.size());
    createInfo.ppEnabledLayerNames = VALIDATION_LAYERS.data();

    if (vkCreateDevice(M_PHYSICALDEVICE, &createInfo, nullptr, &M_DEVICE) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create logical device!");
    }

    vkGetDeviceQueue(M_DEVICE, indices.graphicsFamily.value(), 0, &M_GRAPHICSQUEUE);
    vkGetDeviceQueue(M_DEVICE, indices.presentFamily.value(), 0, &M_PRESENTQUEUE);

    std::cout << "  [createLogicalDevice] SUCCESS - Logical device and queues created.\n";
}
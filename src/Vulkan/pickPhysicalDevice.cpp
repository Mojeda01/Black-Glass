#include "../AlethiaV3.h"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <set>
#include <map>
#include <string>

namespace
{

bool checkDeviceExtensionSupport(VkPhysicalDevice device)
{
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(DEVICE_EXTENSIONS.begin(), DEVICE_EXTENSIONS.end());

    for (const auto& extension : availableExtensions) 
    {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}

} // namespace

QueueFamilyIndices AlethiaV3::findQueueFamilies(VkPhysicalDevice device)
{
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto& queueFamily : queueFamilies)
    {
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            indices.graphicsFamily = i;
        }

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, M_SURFACE, &presentSupport);
        if (presentSupport)
        {
            indices.presentFamily = i;
        }

        if (indices.isComplete())
        {
            break;
        }

        i++;
    }
    return indices;
}

SwapchainSupportDetails AlethiaV3::querySwapchainSupport(VkPhysicalDevice device)
{
    SwapchainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, M_SURFACE, &details.capabilities);
    
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, M_SURFACE, &formatCount, nullptr);
    if (formatCount != 0)
    {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, M_SURFACE, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, M_SURFACE, &presentModeCount, nullptr);
    if (presentModeCount != 0)
    {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, M_SURFACE, &presentModeCount, details.presentModes.data());
    }
    return details;
}

namespace 
{

int rateDeviceSuitability(VkPhysicalDevice device, const QueueFamilyIndices& indices, 
    const SwapchainSupportDetails& swapchainSupport)
{

    if (!indices.isComplete())
    {
        return 0;
    }

    if (!checkDeviceExtensionSupport(device))
    {
        return 0;
    }
 
    if (swapchainSupport.formats.empty() || swapchainSupport.presentModes.empty())
    {
        return 0;
    }

    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(device, &deviceProperties);

    int score = 1;
    if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
    {
        score += 1000;
    }

    else if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
    {
        score += 500;
    }

    return score;
}

} // namespace.

void AlethiaV3::pickPhysicalDevice()
{
    std::cout << "  [pickPhysicalDevice] Starting...\n";

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(M_INSTANCE, &deviceCount, nullptr);

    if (deviceCount == 0)
    {
        throw std::runtime_error("Failed to find GPUs with Vulkan support!");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(M_INSTANCE, &deviceCount, devices.data());

    std::multimap<int, VkPhysicalDevice> candidates;

    for (const auto& device : devices)
    {
        QueueFamilyIndices indices = findQueueFamilies(device);
        SwapchainSupportDetails swapchainSupport = querySwapchainSupport(device);
        int score = rateDeviceSuitability(device, indices, swapchainSupport); 
        candidates.insert(std::make_pair(score, device));
    }

    if (candidates.empty() || candidates.rbegin()->first == 0)
    {
        throw std::runtime_error("Failed to find a suitable GPU!");
    }

    M_PHYSICALDEVICE = candidates.rbegin()->second;

    VkPhysicalDeviceProperties chosenProps;
    vkGetPhysicalDeviceProperties(M_PHYSICALDEVICE, &chosenProps);
    std::cout << "  [pickPhysicalDevice] SUCCESS - Selected: " << chosenProps.deviceName << "\n";
}
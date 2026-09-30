#include "../AlethiaV3.h"
#include <iostream>
#include <stdexcept>

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) 
    {
        std::cout   << "  [Validation Warning] "
                    << pCallbackData->messageIdNumber   << " : "
                    << pCallbackData->pMessageIdName    << " : "
                    << pCallbackData->pMessage << "\n";
    } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
        std::cerr   << "    [Validation Error] "
                    << pCallbackData->messageIdNumber   << " : "
                    << pCallbackData->pMessageIdName    << " : "
                    << pCallbackData->pMessage          << "\n";
    }
    return VK_FALSE;
}

void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo)
{
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | 
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;
}

void AlethiaV3::setupDebugMessenger()
{
    std::cout << " [setupDebugMessenger] Starting ... \n";

    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    populateDebugMessengerCreateInfo(createInfo);

    auto func = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(M_INSTANCE, "vkCreateDebugUtilsMessengerEXT");

    if (func == nullptr)
        throw std::runtime_error("Failed to load vkCreateDebugUtilsMessengerEXT!");
    
    if (func(M_INSTANCE, &createInfo, nullptr, &M_DEBUGMESSENGER) != VK_SUCCESS)
        throw std::runtime_error("Failed to create debug messenger!");
    
    std::cout << "  [setupDebugMessenger] SUCCESS - Debug messenger active. \n";
}

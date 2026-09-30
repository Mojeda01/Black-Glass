#include "../AlethiaV3.h"
#include <iostream>
#include <stdexcept>

void AlethiaV3::createImageViews()
{
    std::cout << "  [createImageViews] Starting ...\n";

    M_SWAPCHAINIMAGEVIEWS.resize(M_SWAPCHAINIMAGES.size());

    for (size_t i = 0; i < M_SWAPCHAINIMAGES.size(); i++)
    {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = M_SWAPCHAINIMAGES[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = M_SWAPCHAINIMAGEFORMAT;

        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;
        
        if (vkCreateImageView(M_DEVICE, &createInfo, nullptr, &M_SWAPCHAINIMAGEVIEWS[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create image view!");
        }
    }

    std::cout << " [createImageViews] SUCCESS :: CREATED " << M_SWAPCHAINIMAGEVIEWS.size() << " images views. \n";
}
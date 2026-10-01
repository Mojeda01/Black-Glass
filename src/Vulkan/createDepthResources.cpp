#include "../AlethiaV3.h"
#include <iostream>
#include <stdexcept>
#include <vector>

VkFormat AlethiaV3::findDepthFormat()
{
    const std::vector<VkFormat> candidates = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT
    };

    for (VkFormat format : candidates)
    {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(M_PHYSICALDEVICE, format, &props);

        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        {
            return format;
        }
    }

    throw std::runtime_error("Failed to find a supported depth format!");
}

void AlethiaV3::createDepthResources()
{
    VkFormat depthFormat = findDepthFormat();

    M_DEPTHIMAGE = createImage(
        M_SWAPCHAINEXTENT.width,
        M_SWAPCHAINEXTENT.height,
        depthFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT
    );

    std::cout << "  [createDepthResources] SUCCESS - Depth image created (format "
                    << depthFormat << ").\n";
}

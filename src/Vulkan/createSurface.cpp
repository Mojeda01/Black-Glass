#include "../AlethiaV3.h"
#include <iostream>
#include <stdexcept>

void AlethiaV3::createSurface()
{
    std::cout << " [createSurface] Starting .. \n";

    if (glfwCreateWindowSurface(M_INSTANCE, m_window, nullptr, &M_SURFACE) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create window surface!");
    }

    std::cout << "  [createSurface] SUCCESS, Window surface created. \n";
}
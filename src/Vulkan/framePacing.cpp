#include "../BlackGlass.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <thread>

namespace
{
    constexpr double RECHECK_SECONDS = 1.0;
    constexpr double SPIN_SECONDS = 0.0007;

    GLFWmonitor* monitorOf(GLFWwindow* window)
    {
        if (GLFWmonitor* fullscreen = glfwGetWindowMonitor(window))
        {
            return fullscreen;
        }

        int windowX = 0;
        int windowY = 0;
        int windowW = 0;
        int windowH = 0;
        glfwGetWindowPos(window, &windowX, &windowY);
        glfwGetWindowSize(window, &windowW, &windowH);

        int count = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&count);
        GLFWmonitor* best = glfwGetPrimaryMonitor();
        long long bestArea = 0;
        for (int i = 0; i < count; i++)
        {
            const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
            if (mode == nullptr)
            {
                continue;
            }
            int monitorX = 0;
            int monitorY = 0;
            glfwGetMonitorPos(monitors[i], &monitorX, &monitorY);
            const int overlapW = std::min(windowX + windowW, monitorX + mode->width) - std::max(windowX, monitorX);
            const int overlapH = std::min(windowY + windowH, monitorY + mode->height) - std::max(windowY, monitorY);
            if (overlapW > 0 && overlapH > 0 && static_cast<long long>(overlapW) * overlapH > bestArea)
            {
                bestArea = static_cast<long long>(overlapW) * overlapH;
                best = monitors[i];
            }
        }
        return best;
    }

    double configuredCap()
    {
        static const double cap = []
        {
            const char* value = std::getenv("BLACK_GLASS_FRAME_CAP");
            return value != nullptr ? std::atof(value) : -1.0;
        }();
        return cap;
    }
}

void BlackGlass::paceFrame()
{
    double now = glfwGetTime();

    if (now - m_paceChecked >= RECHECK_SECONDS)
    {
        m_paceChecked = now;
        double hz = configuredCap();
        if (hz < 0.0)
        {
            GLFWmonitor* monitor = monitorOf(m_window);
            const GLFWvidmode* mode = monitor != nullptr ? glfwGetVideoMode(monitor) : nullptr;
            hz = mode != nullptr ? static_cast<double>(mode->refreshRate) : 0.0;
        }
        m_paceInterval = hz > 1.0 ? 1.0 / hz : 0.0;
    }

    if (m_paceInterval <= 0.0)
    {
        m_paceNext = 0.0;
        return;
    }

    if (m_paceNext <= 0.0)
    {
        m_paceNext = now;
    }

    if (now < m_paceNext)
    {
        const double sleepSeconds = m_paceNext - now - SPIN_SECONDS;
        if (sleepSeconds > 0.0)
        {
            std::this_thread::sleep_for(std::chrono::duration<double>(sleepSeconds));
        }
        while (glfwGetTime() < m_paceNext)
        {
        }
    }

    m_paceNext += m_paceInterval;
    now = glfwGetTime();
    if (m_paceNext < now)
    {
        m_paceNext = now;
    }
}

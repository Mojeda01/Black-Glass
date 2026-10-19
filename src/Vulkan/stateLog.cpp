#include "../BlackGlass.h"
#include "../Debug/CocoaProbe.h"
#include "../Debug/StateLog.h"
#include "../Debug/DisplayTiming.h"
#ifdef __APPLE__
#include <crt_externs.h>
#else
extern char** environ;
#endif
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>

namespace
{
    constexpr double COCOA_PROBE_SECONDS = 0.25;

    CocoaProbeInfo g_cocoa;
    double g_cocoaProbeTime = -1.0;

    bool g_hasPrev = false;
    glm::vec3 g_prevPosition{ 0.0f };
    float g_prevYaw = 0.0f;
    float g_prevPitch = 0.0f;
    double g_prevTime = 0.0;
    double g_windowStart = 0.0;
    double g_distance = 0.0;
    double g_turn = 0.0;
    double g_maxFrameMs = 0.0;
    uint32_t g_frames = 0;

    std::string g_device;
    std::string g_driver;
    std::string g_api;
    std::string g_env;

    std::string cleaned(std::string text)
    {
        for (char& c : text)
        {
            if (c == ',' || c == '\n')
            {
                c = ';';
            }
        }
        return text;
    }

    std::string environmentSummary()
    {
#ifdef __APPLE__
        char** variables = *_NSGetEnviron();
#else
        char** variables = environ;
#endif
        std::string joined;
        for (char** entry = variables; entry != nullptr && *entry != nullptr; entry++)
        {
            const std::string text = *entry;
            if (text.rfind("MVK_", 0) == 0 || text.rfind("VK_", 0) == 0 || text.rfind("METAL_", 0) == 0 || text.rfind("MTL_", 0) == 0)
            {
                joined += (joined.empty() ? "" : "|") + text;
            }
        }
        if (joined.size() > 400)
        {
            joined.resize(400);
        }
        return cleaned(joined.empty() ? "none" : joined);
    }

    std::string formatted(const char* format, ...) __attribute__((format(printf, 1, 2)));

    std::string formatted(const char* format, ...)
    {
        char buffer[4096];
        va_list args;
        va_start(args, format);
        std::vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        return buffer;
    }

    float angleDelta(float from, float to)
    {
        float delta = std::fmod(to - from, 360.0f);
        if (delta > 180.0f)
        {
            delta -= 360.0f;
        }
        else if (delta < -180.0f)
        {
            delta += 360.0f;
        }
        return delta;
    }
}

void BlackGlass::updateStateLog()
{
    const double now = glfwGetTime();

    if (!g_hasPrev)
    {
        g_hasPrev = true;
        g_prevPosition = m_camera.position;
        g_prevYaw = m_camera.yaw;
        g_prevPitch = m_camera.pitch;
        g_prevTime = now;
        g_windowStart = now;

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(M_PHYSICALDEVICE, &properties);
        g_device = cleaned(properties.deviceName);

        VkPhysicalDeviceDriverProperties driverProperties{};
        driverProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
        VkPhysicalDeviceProperties2 properties2{};
        properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties2.pNext = &driverProperties;
        vkGetPhysicalDeviceProperties2(M_PHYSICALDEVICE, &properties2);
        g_driver = cleaned(std::string(driverProperties.driverName) + "|" + driverProperties.driverInfo);
        g_api = formatted("%u.%u.%u/drv%u", VK_API_VERSION_MAJOR(properties.apiVersion),
                          VK_API_VERSION_MINOR(properties.apiVersion), VK_API_VERSION_PATCH(properties.apiVersion),
                          properties.driverVersion);
        g_env = environmentSummary();
    }

    g_distance += glm::length(m_camera.position - g_prevPosition);
    g_turn += std::fabs(angleDelta(g_prevYaw, m_camera.yaw)) + std::fabs(angleDelta(g_prevPitch, m_camera.pitch));
    g_maxFrameMs = std::max(g_maxFrameMs, (now - g_prevTime) * 1000.0);
    g_prevPosition = m_camera.position;
    g_prevYaw = m_camera.yaw;
    g_prevPitch = m_camera.pitch;
    g_prevTime = now;
    g_frames++;

    if (now - g_cocoaProbeTime >= COCOA_PROBE_SECONDS)
    {
        g_cocoaProbeTime = now;
        probeCocoaWindow(m_window, g_cocoa);
    }

    const FrameLogView view = frameLogView();
    const char* mode = m_walkMode ? "walk" : (m_orthoMode != 0 ? "ortho" : "editor");

    std::string discrete = formatted(
        "%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%zu,%zu,%d,%u,%u,%d,%d,%d,%d,%s,%u,%s,",
        mode, m_mouseCaptured, m_showUi, static_cast<int>(M_MSAASAMPLES),
        m_settings.shadows, m_settings.bloom, m_settings.ssao, m_ssaoActive,
        m_settings.reflections, m_reflectionActive, m_settings.tonemapper, m_debugView,
        M_MODELS.size(), m_selection.size(), m_gpuTimingsEnabled,
        view.extentW, view.extentH, view.fbW, view.fbH, view.winW, view.winH,
        view.presentMode, view.imageCount, g_device.c_str());
#ifdef NDEBUG
    const char* buildType = "release";
#else
    const char* buildType = "debug";
#endif
    discrete += formatted("%s,%s,%s,%zu,%s,", g_driver.c_str(), g_api.c_str(), g_env.c_str(),
                          VALIDATION_LAYERS.size(), buildType);

    discrete += formatted(
        "%d,%d,%d,%d,%d,%d,%.0f,%.0f,%.2f,%s,%d,%d,%d,%.1f,%u,%d,%d,%s,%.3f,%d,%d,%d,%d,%.2f,%d,%d,%d",
        g_cocoa.available, g_cocoa.fullscreen, g_cocoa.visible, g_cocoa.displaySync,
        g_cocoa.drawableCount, g_cocoa.opaque, g_cocoa.layerW, g_cocoa.layerH, g_cocoa.scale,
        g_cocoa.screenName, g_cocoa.screenMaxHz, g_cocoa.modeW, g_cocoa.modeH, g_cocoa.modeHz,
        g_cocoa.displayId, g_cocoa.primary, g_cocoa.screenCount, g_cocoa.screens, g_cocoa.linkNominalMs,
        g_cocoa.appActive, g_cocoa.keyWindow, g_cocoa.miniaturized, g_cocoa.onActiveSpace,
        g_cocoa.screenFraction, g_cocoa.screensTouched, g_cocoa.presentsWithTransaction, g_cocoa.drawableTimeout);

    const char* reason = StateLog::reason(now, discrete);
    if (reason == nullptr)
    {
        return;
    }

    const double span = std::max(now - g_windowStart, 1e-6);
    VmaBudget budgets[VK_MAX_MEMORY_HEAPS] = {};
    vmaGetHeapBudgets(M_ALLOCATOR, budgets);
    double usedMb = 0.0;
    double budgetMb = 0.0;
    for (const VmaBudget& budget : budgets)
    {
        usedMb += static_cast<double>(budget.usage) / (1024.0 * 1024.0);
        budgetMb += static_cast<double>(budget.budget) / (1024.0 * 1024.0);
    }

    std::string continuous = formatted(
        "%.1f,%.2f,%.3f,%.1f",
        g_frames / span, g_maxFrameMs, g_distance / span, g_turn / span);
    for (uint32_t i = 0; i < GPU_SEGMENTS; i++)
    {
        continuous += formatted(",%.3f", m_gpuSegmentsMs[i]);
    }
    continuous += formatted(",%.1f,%.1f", usedMb, budgetMb);

    const DisplayTimingSummary timing = DisplayTiming::takeSummary();
    continuous += formatted(",%.2f,%.2f,%.3f,%.3f,%u,%u,%.5f,%.3f,%.3f",
                            timing.vblankHz, timing.presentsPerVblank, timing.phaseLock, timing.phaseMean,
                            timing.repeats, timing.dropped, timing.stepMean, timing.stepCv, g_cocoa.linkActualMs);
    continuous += formatted(",%.1f", m_paceInterval > 0.0 ? 1.0 / m_paceInterval : 0.0);

    StateLog::write(now, reason, discrete, continuous);

    g_windowStart = now;
    g_distance = 0.0;
    g_turn = 0.0;
    g_maxFrameMs = 0.0;
    g_frames = 0;
}

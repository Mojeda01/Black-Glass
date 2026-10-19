#pragma once
#include <cstdint>

struct GLFWwindow;

struct CocoaProbeInfo
{
    bool available = false;
    bool fullscreen = false;
    bool visible = true;
    bool displaySync = false;
    bool opaque = false;
    bool primary = false;
    bool appActive = false;
    bool keyWindow = false;
    bool miniaturized = false;
    bool onActiveSpace = false;
    bool presentsWithTransaction = false;
    bool drawableTimeout = false;
    int screensTouched = 0;
    double screenFraction = 0.0;
    int drawableCount = 0;
    double layerW = 0.0;
    double layerH = 0.0;
    double scale = 0.0;
    int screenMaxHz = 0;
    int modeW = 0;
    int modeH = 0;
    double modeHz = 0.0;
    uint32_t displayId = 0;
    int screenCount = 0;
    double linkNominalMs = 0.0;
    double linkActualMs = 0.0;
    char screenName[64] = {};
    char screens[256] = {};
};

bool probeCocoaWindow(GLFWwindow* window, CocoaProbeInfo& info);
void shutdownCocoaProbe();

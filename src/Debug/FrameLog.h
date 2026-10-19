#pragma once
#include <cstdint>

struct FrameLogSample
{
    double time = 0.0;
    double frameMs = 0.0;
    double fenceMs = 0.0;
    double acquireMs = 0.0;
    double cpuMs = 0.0;
    double presentMs = 0.0;
    int acquireResult = 0;
    int presentResult = 0;
    uint32_t imageIndex = 0;
    uint32_t frameSlot = 0;
    double presentAt = 0.0;
    double phase = -1.0;
    double camX = 0.0;
    double camY = 0.0;
    double camZ = 0.0;
    double camYaw = 0.0;
    double camPitch = 0.0;
    double updateMs = 0.0;
    double recordMs = 0.0;
    double submitMs = 0.0;
};

struct FrameLogView
{
    uint32_t extentW = 0;
    uint32_t extentH = 0;
    int fbW = 0;
    int fbH = 0;
    int winW = 0;
    int winH = 0;
    const char* presentMode = "";
    uint32_t imageCount = 0;
};

namespace FrameLog
{
    void open();
    void close();
    void frame(const FrameLogSample& sample, const FrameLogView& view);
    void event(const char* name, double time, double valueMs, const FrameLogView& view);
}

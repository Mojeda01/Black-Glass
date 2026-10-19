#pragma once
#include <cstdint>

struct CameraPose
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float yaw = 0.0f;
    float pitch = 0.0f;
};

struct DisplayTimingSummary
{
    uint32_t vblanks = 0;
    uint32_t presents = 0;
    uint32_t repeats = 0;
    uint32_t dropped = 0;
    double presentsPerVblank = 0.0;
    double vblankHz = 0.0;
    double phaseLock = 0.0;
    double phaseMean = 0.0;
    double stepMean = 0.0;
    double stepCv = 0.0;
};

namespace DisplayTiming
{
    void pushVblank(double time);
    double notePresent(double time, const CameraPose& pose);
    DisplayTimingSummary takeSummary();
}

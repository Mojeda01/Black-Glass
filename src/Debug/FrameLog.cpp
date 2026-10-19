#include "FrameLog.h"
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace
{
    std::ofstream g_file;
    double g_flushTime = 0.0;

    void writeRow(const char* kind, const char* name, double time, const FrameLogSample* sample,
                  double valueMs, const FrameLogView& view)
    {
        if (!g_file.is_open())
        {
            return;
        }

        FrameLogSample empty;
        const FrameLogSample& s = sample ? *sample : empty;

        char row[512];
        std::snprintf(row, sizeof(row),
                      "%.4f,%s,%s,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%u,%u,%u,%u,%d,%d,%d,%d,%s,%u,%.3f,%.4f,%.4f,%.4f,%.4f,%.4f,%.3f,%.3f,%.3f,%.3f,%.3f\n",
                      time, kind, name,
                      s.frameMs, s.fenceMs, s.acquireMs, s.cpuMs, s.presentMs,
                      s.acquireResult, s.presentResult, s.imageIndex, s.frameSlot,
                      view.extentW, view.extentH, view.fbW, view.fbH, view.winW, view.winH,
                      view.presentMode, view.imageCount, valueMs,
                      s.presentAt, s.phase, s.camX, s.camY, s.camZ, s.camYaw, s.camPitch,
                      s.updateMs, s.recordMs, s.submitMs);
        g_file << row;

        if (sample == nullptr || time - g_flushTime >= 0.5)
        {
            g_file.flush();
            g_flushTime = time;
        }
    }
}

void FrameLog::open()
{
    std::error_code error;
    std::filesystem::create_directories(BLACK_GLASS_LOG_DIR, error);
    std::time_t stamp = std::time(nullptr);
    char name[64];
    std::strftime(name, sizeof(name), "frame_%Y%m%d_%H%M%S.csv", std::localtime(&stamp));
    g_file.open(std::string(BLACK_GLASS_LOG_DIR) + "/" + name);
    g_flushTime = 0.0;
    if (g_file.is_open())
    {
        g_file << "time,kind,name,frameMs,fenceMs,acquireMs,cpuMs,presentMs,acquireResult,presentResult,imageIndex,slot,extentW,extentH,fbW,fbH,winW,winH,presentMode,images,valueMs,presentAt,phase,camX,camY,camZ,camYaw,camPitch,updateMs,recordMs,submitMs\n";
    }
}

void FrameLog::close()
{
    if (g_file.is_open())
    {
        g_file.close();
    }
}

void FrameLog::frame(const FrameLogSample& sample, const FrameLogView& view)
{
    writeRow("frame", "", sample.time, &sample, 0.0, view);
}

void FrameLog::event(const char* name, double time, double valueMs, const FrameLogView& view)
{
    writeRow("event", name, time, nullptr, valueMs, view);
}

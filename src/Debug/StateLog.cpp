#include "StateLog.h"
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace
{
    constexpr double TICK_SECONDS = 1.0;

    std::ofstream g_file;
    std::string g_lastDiscrete;
    double g_lastWrite = -1.0;
    bool g_hasLast = false;
}

void StateLog::open()
{
    std::error_code error;
    std::filesystem::create_directories(BLACK_GLASS_LOG_DIR, error);
    std::time_t stamp = std::time(nullptr);
    char name[64];
    std::strftime(name, sizeof(name), "state_%Y%m%d_%H%M%S.csv", std::localtime(&stamp));
    g_file.open(std::string(BLACK_GLASS_LOG_DIR) + "/" + name);
    g_lastDiscrete.clear();
    g_lastWrite = -1.0;
    g_hasLast = false;
    if (g_file.is_open())
    {
        g_file << "time,reason,mode,mouseCaptured,uiVisible,msaa,shadows,bloom,ssao,ssaoActive,reflections,reflectionActive,"
                  "tonemapper,debugView,models,selected,gpuTimingsOn,extentW,extentH,fbW,fbH,winW,winH,presentMode,images,device,driver,api,env,validation,build,"
                  "cocoa,fullscreen,visible,displaySync,drawableCount,opaque,layerW,layerH,scale,screenName,screenMaxHz,modeW,modeH,modeHz,displayId,primary,screenCount,screens,linkNominalMs,"
                  "appActive,keyWindow,miniaturized,onActiveSpace,screenFraction,screensTouched,presentsWithTransaction,drawableTimeout,"
                  "fps,maxFrameMs,camSpeed,turnDegPerSec,gpuSsao,gpuRefl,gpuMain,gpuGlass,gpuBloom,gpuComposite,heapUsedMB,heapBudgetMB,vblankHz,presentsPerVblank,phaseLock,phaseMean,repeats,dropped,stepMean,stepCv,linkActualMs,paceHz\n";
    }
}

void StateLog::close()
{
    if (g_file.is_open())
    {
        g_file.close();
    }
}

const char* StateLog::reason(double time, const std::string& discrete)
{
    if (!g_file.is_open())
    {
        return nullptr;
    }
    if (!g_hasLast)
    {
        return "init";
    }
    if (discrete != g_lastDiscrete)
    {
        return "change";
    }
    if (time - g_lastWrite >= TICK_SECONDS)
    {
        return "tick";
    }
    return nullptr;
}

void StateLog::write(double time, const char* reason, const std::string& discrete, const std::string& continuous)
{
    if (!g_file.is_open())
    {
        return;
    }
    char head[64];
    std::snprintf(head, sizeof(head), "%.4f,%s,", time, reason);
    g_file << head << discrete << ',' << continuous << '\n';
    g_file.flush();
    g_lastDiscrete = discrete;
    g_lastWrite = time;
    g_hasLast = true;
}

#include "DisplayTiming.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <deque>
#include <vector>

namespace
{
    constexpr size_t RING_SIZE = 256;
    constexpr size_t KEEP_VBLANKS = 512;
    constexpr size_t PERIOD_SAMPLES = 32;
    constexpr double TWO_PI = 6.283185307179586;
    constexpr double DEG_TO_RAD = 0.017453292519943295;

    struct PendingPresent
    {
        double time = 0.0;
        CameraPose pose;
    };

    std::array<double, RING_SIZE> g_ring{};
    std::atomic<uint64_t> g_head{ 0 };
    uint64_t g_tail = 0;

    std::vector<double> g_vblanks;
    std::deque<PendingPresent> g_pending;
    double g_processedUntil = -1.0;
    bool g_hasShown = false;
    PendingPresent g_shown;

    uint32_t g_presents = 0;
    uint32_t g_vblankCount = 0;
    uint32_t g_repeats = 0;
    uint32_t g_dropped = 0;
    uint32_t g_phaseCount = 0;
    double g_phaseCos = 0.0;
    double g_phaseSin = 0.0;
    uint32_t g_steps = 0;
    double g_stepSum = 0.0;
    double g_stepSumSq = 0.0;

    void drain()
    {
        const uint64_t head = g_head.load(std::memory_order_acquire);
        if (head - g_tail > RING_SIZE / 2)
        {
            g_tail = head - RING_SIZE / 2;
        }
        while (g_tail < head)
        {
            const double time = g_ring[g_tail % RING_SIZE];
            if (g_vblanks.empty() || time > g_vblanks.back())
            {
                g_vblanks.push_back(time);
            }
            g_tail++;
        }
        if (g_vblanks.size() > KEEP_VBLANKS * 2)
        {
            g_vblanks.erase(g_vblanks.begin(), g_vblanks.end() - KEEP_VBLANKS);
        }
    }

    double period()
    {
        if (g_vblanks.size() < 4)
        {
            return 0.0;
        }
        const size_t count = std::min(g_vblanks.size() - 1, PERIOD_SAMPLES);
        std::vector<double> deltas;
        deltas.reserve(count);
        for (size_t i = g_vblanks.size() - count; i < g_vblanks.size(); i++)
        {
            deltas.push_back(g_vblanks[i] - g_vblanks[i - 1]);
        }
        std::nth_element(deltas.begin(), deltas.begin() + deltas.size() / 2, deltas.end());
        return deltas[deltas.size() / 2];
    }

    double phaseOf(double time, double vblankPeriod)
    {
        if (vblankPeriod <= 0.0 || g_vblanks.empty())
        {
            return -1.0;
        }
        auto after = std::upper_bound(g_vblanks.begin(), g_vblanks.end(), time);
        if (after == g_vblanks.begin())
        {
            return -1.0;
        }
        double previous = *(after - 1);
        previous += std::floor((time - previous) / vblankPeriod) * vblankPeriod;
        const double phase = (time - previous) / vblankPeriod;
        return std::clamp(phase, 0.0, 0.999999);
    }

    double poseDistance(const CameraPose& a, const CameraPose& b)
    {
        const double dx = b.x - a.x;
        const double dy = b.y - a.y;
        const double dz = b.z - a.z;
        double dyaw = std::fmod(static_cast<double>(b.yaw) - a.yaw, 360.0);
        if (dyaw > 180.0)
        {
            dyaw -= 360.0;
        }
        else if (dyaw < -180.0)
        {
            dyaw += 360.0;
        }
        const double dpitch = static_cast<double>(b.pitch) - a.pitch;
        return std::sqrt(dx * dx + dy * dy + dz * dz) + (std::fabs(dyaw) + std::fabs(dpitch)) * DEG_TO_RAD;
    }

    void processVblanks(double upTo)
    {
        auto it = std::upper_bound(g_vblanks.begin(), g_vblanks.end(), g_processedUntil);
        for (; it != g_vblanks.end() && *it < upTo; ++it)
        {
            const double vblank = *it;
            uint32_t popped = 0;
            PendingPresent latest;
            while (!g_pending.empty() && g_pending.front().time <= vblank)
            {
                latest = g_pending.front();
                g_pending.pop_front();
                popped++;
            }

            g_vblankCount++;
            if (popped > 1)
            {
                g_dropped += popped - 1;
            }

            if (g_hasShown)
            {
                double step = 0.0;
                if (popped == 0)
                {
                    g_repeats++;
                }
                else
                {
                    step = poseDistance(g_shown.pose, latest.pose);
                }
                g_steps++;
                g_stepSum += step;
                g_stepSumSq += step * step;
            }

            if (popped > 0)
            {
                g_shown = latest;
                g_hasShown = true;
            }
            g_processedUntil = vblank;
        }
    }
}

void DisplayTiming::pushVblank(double time)
{
    const uint64_t head = g_head.load(std::memory_order_relaxed);
    g_ring[head % RING_SIZE] = time;
    g_head.store(head + 1, std::memory_order_release);
}

double DisplayTiming::notePresent(double time, const CameraPose& pose)
{
    drain();
    const double vblankPeriod = period();
    const double phase = phaseOf(time, vblankPeriod);

    g_presents++;
    if (phase >= 0.0)
    {
        g_phaseCount++;
        g_phaseCos += std::cos(phase * TWO_PI);
        g_phaseSin += std::sin(phase * TWO_PI);
    }

    PendingPresent entry;
    entry.time = time;
    entry.pose = pose;
    g_pending.push_back(entry);
    processVblanks(time);
    return phase;
}

DisplayTimingSummary DisplayTiming::takeSummary()
{
    drain();
    DisplayTimingSummary summary;
    summary.vblanks = g_vblankCount;
    summary.presents = g_presents;
    summary.repeats = g_repeats;
    summary.dropped = g_dropped;
    summary.presentsPerVblank = g_vblankCount > 0 ? static_cast<double>(g_presents) / g_vblankCount : 0.0;

    const double vblankPeriod = period();
    summary.vblankHz = vblankPeriod > 0.0 ? 1.0 / vblankPeriod : 0.0;

    if (g_phaseCount > 0)
    {
        summary.phaseLock = std::sqrt(g_phaseCos * g_phaseCos + g_phaseSin * g_phaseSin) / g_phaseCount;
        double angle = std::atan2(g_phaseSin, g_phaseCos) / TWO_PI;
        if (angle < 0.0)
        {
            angle += 1.0;
        }
        summary.phaseMean = angle;
    }

    if (g_steps > 0)
    {
        const double mean = g_stepSum / g_steps;
        const double variance = std::max(g_stepSumSq / g_steps - mean * mean, 0.0);
        summary.stepMean = mean;
        summary.stepCv = mean > 1e-9 ? std::sqrt(variance) / mean : 0.0;
    }

    g_presents = 0;
    g_vblankCount = 0;
    g_repeats = 0;
    g_dropped = 0;
    g_phaseCount = 0;
    g_phaseCos = 0.0;
    g_phaseSin = 0.0;
    g_steps = 0;
    g_stepSum = 0.0;
    g_stepSumSq = 0.0;
    return summary;
}

#pragma once

enum class Tonemapper : int
{
    Custom = 0,
    ACES = 1,
    AgX = 2,
    Neutral = 3
};

constexpr const char* TONEMAPPER_NAMES[] = { "Custom", "ACES", "AgX", "Khronos Neutral" };
constexpr int TONEMAPPER_COUNT = 4;

struct RenderSettings
{
    float sunIntensity = 3.0f;
    float sunAzimuth = 0.0f;
    float sunElevation = 60.0f;
    float envIntensity = 1.0f;
    bool shadows = true;

    float exposure = 1.0f;
    bool bloom = true;
    float bloomStrength = 0.04f;
    int tonemapper = static_cast<int>(Tonemapper::Custom);

    bool ssao = true;
    float ssaoRadius = 0.5f;
    float ssaoIntensity = 1.0f;

    bool reflections = true;
    float reflectionStrength = 1.0f;
    float floorRoughness = 0.25f;

    bool operator==(const RenderSettings&) const = default;
};

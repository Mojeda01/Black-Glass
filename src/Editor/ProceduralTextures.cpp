#include "ProceduralTextures.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace
{
    constexpr int SIZE = 512;

    struct Color
    {
        float r;
        float g;
        float b;
    };

    uint32_t hashCell(int x, int y, uint32_t seed)
    {
        uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + seed * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return h ^ (h >> 16);
    }

    int wrap(int value, int period)
    {
        return ((value % period) + period) % period;
    }

    float cellValue(int x, int y, int periodX, int periodY, uint32_t seed)
    {
        return static_cast<float>(hashCell(wrap(x, periodX), wrap(y, periodY), seed) & 0xFFFFu) / 65535.0f;
    }

    float smooth(float t)
    {
        return t * t * (3.0f - 2.0f * t);
    }

    float mix(float a, float b, float t)
    {
        return a + (b - a) * t;
    }

    float valueNoise(float x, float y, int periodX, int periodY, uint32_t seed)
    {
        int x0 = static_cast<int>(std::floor(x));
        int y0 = static_cast<int>(std::floor(y));
        float fx = smooth(x - static_cast<float>(x0));
        float fy = smooth(y - static_cast<float>(y0));
        float a = cellValue(x0, y0, periodX, periodY, seed);
        float b = cellValue(x0 + 1, y0, periodX, periodY, seed);
        float c = cellValue(x0, y0 + 1, periodX, periodY, seed);
        float d = cellValue(x0 + 1, y0 + 1, periodX, periodY, seed);
        return mix(mix(a, b, fx), mix(c, d, fx), fy);
    }

    float fractal(float u, float v, int basePeriod, int octaves, uint32_t seed)
    {
        float sum = 0.0f;
        float amplitude = 0.5f;
        float norm = 0.0f;
        int period = basePeriod;
        for (int octave = 0; octave < octaves; octave++)
        {
            sum += amplitude * valueNoise(u * static_cast<float>(period), v * static_cast<float>(period),
                                          period, period, seed + static_cast<uint32_t>(octave) * 17u);
            norm += amplitude;
            amplitude *= 0.5f;
            period *= 2;
        }
        return sum / norm;
    }

    float grain(int x, int y, uint32_t seed)
    {
        return static_cast<float>(hashCell(x, y, seed) & 0xFFu) / 255.0f - 0.5f;
    }

    float fraction(float value)
    {
        return value - std::floor(value);
    }

    void put(ImageData& image, int x, int y, Color color)
    {
        size_t offset = (static_cast<size_t>(y) * SIZE + static_cast<size_t>(x)) * 4;
        image.pixels[offset + 0] = static_cast<unsigned char>(std::clamp(color.r, 0.0f, 1.0f) * 255.0f + 0.5f);
        image.pixels[offset + 1] = static_cast<unsigned char>(std::clamp(color.g, 0.0f, 1.0f) * 255.0f + 0.5f);
        image.pixels[offset + 2] = static_cast<unsigned char>(std::clamp(color.b, 0.0f, 1.0f) * 255.0f + 0.5f);
        image.pixels[offset + 3] = 255;
    }

    template <typename Shader>
    void fill(ImageData& image, Shader shader)
    {
        image.width = SIZE;
        image.height = SIZE;
        image.pixels.assign(static_cast<size_t>(SIZE) * SIZE * 4, 255);
        for (int y = 0; y < SIZE; y++)
        {
            for (int x = 0; x < SIZE; x++)
            {
                float u = (static_cast<float>(x) + 0.5f) / SIZE;
                float v = (static_cast<float>(y) + 0.5f) / SIZE;
                put(image, x, y, shader(x, y, u, v));
            }
        }
    }

    Color checker(int, int, float u, float v)
    {
        int cell = static_cast<int>(std::floor(u * 8.0f)) + static_cast<int>(std::floor(v * 8.0f));
        return (cell & 1) ? Color{ 0.22f, 0.22f, 0.24f } : Color{ 0.86f, 0.86f, 0.86f };
    }

    Color grid(int, int, float u, float v)
    {
        float minor = std::min(std::min(fraction(u * 16.0f), 1.0f - fraction(u * 16.0f)),
                               std::min(fraction(v * 16.0f), 1.0f - fraction(v * 16.0f))) * (SIZE / 16.0f);
        float major = std::min(std::min(fraction(u * 4.0f), 1.0f - fraction(u * 4.0f)),
                               std::min(fraction(v * 4.0f), 1.0f - fraction(v * 4.0f))) * (SIZE / 4.0f);
        if (major < 2.5f)
        {
            return { 0.78f, 0.78f, 0.82f };
        }
        if (minor < 1.0f)
        {
            return { 0.5f, 0.5f, 0.54f };
        }
        return { 0.3f, 0.3f, 0.33f };
    }

    Color bricks(int x, int y, float u, float v)
    {
        const int rows = 8;
        const int columns = 4;
        int row = static_cast<int>(std::floor(v * rows));
        float shifted = u * columns + ((row & 1) ? 0.5f : 0.0f);
        int cell = static_cast<int>(std::floor(shifted));
        float fu = shifted - static_cast<float>(cell);
        float fv = v * rows - static_cast<float>(row);

        float cloud = fractal(u, v, 4, 4, 11u);
        float speckle = grain(x, y, 5u) * 0.05f;

        if (fu < 0.035f || fv < 0.075f)
        {
            float tone = 0.62f + (cloud - 0.5f) * 0.25f + speckle;
            return { tone, tone * 0.97f, tone * 0.92f };
        }

        float variation = cellValue(cell, row, columns, rows, 3u) - 0.5f;
        float tone = 1.0f + variation * 0.35f + (cloud - 0.5f) * 0.3f + speckle;
        return { 0.62f * tone, 0.27f * tone, 0.2f * tone };
    }

    Color tiles(int x, int y, float u, float v)
    {
        const int count = 4;
        int cx = static_cast<int>(std::floor(u * count));
        int cy = static_cast<int>(std::floor(v * count));
        float fu = u * count - static_cast<float>(cx);
        float fv = v * count - static_cast<float>(cy);
        float edge = std::min(std::min(fu, 1.0f - fu), std::min(fv, 1.0f - fv));

        if (edge < 0.03f)
        {
            float tone = 0.42f + grain(x, y, 9u) * 0.05f;
            return { tone, tone, tone * 0.96f };
        }

        float variation = cellValue(cx, cy, count, count, 21u) - 0.5f;
        float tone = 0.86f + variation * 0.08f + (fractal(u, v, 4, 3, 31u) - 0.5f) * 0.05f;
        float bevel = std::min(edge / 0.07f, 1.0f);
        tone *= 0.93f + 0.07f * bevel;
        return { tone, tone * 1.01f, tone * 0.98f };
    }

    Color concrete(int x, int y, float u, float v)
    {
        float cloud = fractal(u, v, 4, 5, 41u);
        float pits = fractal(u, v, 32, 2, 43u);
        float tone = 0.5f + (cloud - 0.5f) * 0.32f + grain(x, y, 47u) * 0.06f;
        if (pits > 0.78f)
        {
            tone *= 0.82f;
        }
        return { tone, tone, tone * 1.02f };
    }

    Color planks(int x, int y, float u, float v)
    {
        const int rows = 5;
        int row = static_cast<int>(std::floor(v * rows));
        float fv = v * rows - static_cast<float>(row);

        float offset = cellValue(row, 0, rows, 1, 51u);
        float shiftedU = fraction(u + offset);
        float fiber = valueNoise(shiftedU * 4.0f, v * 80.0f, 4, 80, 53u);
        float rings = valueNoise(shiftedU * 2.0f, v * 10.0f, 2, 10, 57u);
        float tint = cellValue(row, 1, rows, 2, 59u) - 0.5f;

        if (fv < 0.035f)
        {
            return { 0.14f, 0.09f, 0.05f };
        }

        float tone = 1.0f + tint * 0.3f + (fiber - 0.5f) * 0.25f + (rings - 0.5f) * 0.2f + grain(x, y, 61u) * 0.03f;
        return { 0.56f * tone, 0.37f * tone, 0.21f * tone };
    }
}

const std::vector<std::string>& proceduralTextureNames()
{
    static const std::vector<std::string> names = { "Bricks", "Checker", "Concrete", "Grid", "Planks", "Tiles" };
    return names;
}

bool generateProceduralTexture(const std::string& name, ImageData& out)
{
    if (name == "Checker")
    {
        fill(out, checker);
    }
    else if (name == "Grid")
    {
        fill(out, grid);
    }
    else if (name == "Bricks")
    {
        fill(out, bricks);
    }
    else if (name == "Tiles")
    {
        fill(out, tiles);
    }
    else if (name == "Concrete")
    {
        fill(out, concrete);
    }
    else if (name == "Planks")
    {
        fill(out, planks);
    }
    else
    {
        return false;
    }
    return true;
}

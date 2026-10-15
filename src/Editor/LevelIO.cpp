#include "LevelIO.h"

#include "../json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace
{
    using Json = nlohmann::json;

    constexpr int LEVEL_VERSION = 1;

    Json toJson(const glm::vec3& v)
    {
        return Json::array({ v.x, v.y, v.z });
    }

    glm::vec3 toVec3(const Json& j, const glm::vec3& fallback)
    {
        if (!j.is_array() || j.size() != 3)
        {
            return fallback;
        }
        return glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
    }
}

bool saveLevelFile(const std::string& path, const LevelData& level, std::string& error)
{
    Json root;
    root["version"] = LEVEL_VERSION;

    Json brushes = Json::array();
    for (const LevelBrush& brush : level.brushes)
    {
        Json entry;
        entry["name"] = brush.name;
        entry["shape"] = "box";
        entry["size"] = toJson(brush.def.size);
        entry["color"] = toJson(brush.def.color);
        entry["roughness"] = brush.def.roughness;
        entry["metallic"] = brush.def.metallic;
        entry["position"] = toJson(brush.position);
        entry["yaw"] = brush.yawDegrees;
        brushes.push_back(entry);
    }
    root["brushes"] = brushes;

    Json models = Json::array();
    for (const LevelModel& model : level.models)
    {
        Json entry;
        entry["name"] = model.name;
        entry["asset"] = model.asset;
        entry["position"] = toJson(model.position);
        entry["yaw"] = model.yawDegrees;
        entry["scale"] = model.scale;
        models.push_back(entry);
    }
    root["models"] = models;

    if (level.hasSettings)
    {
        const RenderSettings& s = level.settings;
        Json settings;
        settings["sunIntensity"] = s.sunIntensity;
        settings["sunAzimuth"] = s.sunAzimuth;
        settings["sunElevation"] = s.sunElevation;
        settings["envIntensity"] = s.envIntensity;
        settings["shadows"] = s.shadows;
        settings["exposure"] = s.exposure;
        settings["bloom"] = s.bloom;
        settings["bloomStrength"] = s.bloomStrength;
        settings["tonemapper"] = s.tonemapper;
        settings["ssao"] = s.ssao;
        settings["ssaoRadius"] = s.ssaoRadius;
        settings["ssaoIntensity"] = s.ssaoIntensity;
        settings["reflections"] = s.reflections;
        settings["reflectionStrength"] = s.reflectionStrength;
        settings["floorRoughness"] = s.floorRoughness;
        root["settings"] = settings;
    }

    if (level.hasCamera)
    {
        Json camera;
        camera["position"] = toJson(level.camera.position);
        camera["yaw"] = level.camera.yaw;
        camera["pitch"] = level.camera.pitch;
        root["camera"] = camera;
    }

    std::error_code directoryError;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), directoryError);

    std::ofstream out(path);
    if (!out)
    {
        error = "Could not open " + path + " for writing";
        return false;
    }
    out << root.dump(2) << "\n";
    if (!out)
    {
        error = "Failed while writing " + path;
        return false;
    }
    return true;
}

bool loadLevelFile(const std::string& path, LevelData& level, std::string& error)
{
    std::ifstream in(path);
    if (!in)
    {
        error = "Could not open " + path;
        return false;
    }

    try
    {
        Json root = Json::parse(in);

        int version = root.value("version", 0);
        if (version != LEVEL_VERSION)
        {
            error = "Unsupported level version " + std::to_string(version);
            return false;
        }

        LevelData loaded;

        if (root.contains("brushes"))
        {
            for (const Json& entry : root["brushes"])
            {
                LevelBrush brush;
                brush.name = entry.value("name", std::string("Brush"));
                brush.def.size = toVec3(entry.value("size", Json()), brush.def.size);
                brush.def.color = toVec3(entry.value("color", Json()), brush.def.color);
                brush.def.roughness = entry.value("roughness", brush.def.roughness);
                brush.def.metallic = entry.value("metallic", brush.def.metallic);
                brush.position = toVec3(entry.value("position", Json()), glm::vec3(0.0f));
                brush.yawDegrees = entry.value("yaw", 0.0f);
                loaded.brushes.push_back(brush);
            }
        }

        if (root.contains("models"))
        {
            for (const Json& entry : root["models"])
            {
                LevelModel model;
                model.name = entry.value("name", std::string());
                model.asset = entry.value("asset", std::string());
                model.position = toVec3(entry.value("position", Json()), glm::vec3(0.0f));
                model.yawDegrees = entry.value("yaw", 0.0f);
                model.scale = entry.value("scale", 1.0f);
                if (!model.asset.empty())
                {
                    loaded.models.push_back(model);
                }
            }
        }

        if (root.contains("settings") && root["settings"].is_object())
        {
            const Json& settings = root["settings"];
            RenderSettings& s = loaded.settings;
            s.sunIntensity = settings.value("sunIntensity", s.sunIntensity);
            s.sunAzimuth = settings.value("sunAzimuth", s.sunAzimuth);
            s.sunElevation = settings.value("sunElevation", s.sunElevation);
            s.envIntensity = settings.value("envIntensity", s.envIntensity);
            s.shadows = settings.value("shadows", s.shadows);
            s.exposure = settings.value("exposure", s.exposure);
            s.bloom = settings.value("bloom", s.bloom);
            s.bloomStrength = settings.value("bloomStrength", s.bloomStrength);
            s.tonemapper = std::clamp(settings.value("tonemapper", s.tonemapper), 0, TONEMAPPER_COUNT - 1);
            s.ssao = settings.value("ssao", s.ssao);
            s.ssaoRadius = settings.value("ssaoRadius", s.ssaoRadius);
            s.ssaoIntensity = settings.value("ssaoIntensity", s.ssaoIntensity);
            s.reflections = settings.value("reflections", s.reflections);
            s.reflectionStrength = settings.value("reflectionStrength", s.reflectionStrength);
            s.floorRoughness = settings.value("floorRoughness", s.floorRoughness);
            loaded.hasSettings = true;
        }

        if (root.contains("camera") && root["camera"].is_object())
        {
            const Json& camera = root["camera"];
            loaded.camera.position = toVec3(camera.value("position", Json()), loaded.camera.position);
            loaded.camera.yaw = camera.value("yaw", loaded.camera.yaw);
            loaded.camera.pitch = camera.value("pitch", loaded.camera.pitch);
            loaded.hasCamera = true;
        }

        level = std::move(loaded);
        return true;
    }
    catch (const std::exception& e)
    {
        error = std::string("Invalid level file: ") + e.what();
        return false;
    }
}

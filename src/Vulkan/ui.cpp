#include "../BlackGlass.h"
#include "../Editor/Editor.h"
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <stdexcept>

#include "../imgui/imgui.h"
#include "../imgui/imgui_impl_glfw.h"
#include "../imgui/imgui_impl_vulkan.h"

namespace
{

void checkVkResult(VkResult result)
{
    if (result != VK_SUCCESS)
    {
        std::cerr << "[imgui] Vulkan error: " << result << "\n";
    }
}

const char* const DEBUG_VIEW_NAMES[] = {
    "0  Final",
    "1  Final (no normal map)",
    "2  Shading normal",
    "3  Ambient occlusion",
    "4  Base color",
    "5  Roughness / metallic",
    "6  Specular IBL",
    "7  Diffuse + direct",
    "8  Facing",
    "9  Vertex normal"
};

constexpr const char* SETTINGS_FILE = "black_glass_settings.cfg";

bool matchesSearch(const std::string& name, const char* query)
{
    if (query[0] == '\0')
    {
        return true;
    }
    std::string haystack = name;
    std::string needle = query;
    auto lower = [](std::string& s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    };
    lower(haystack);
    lower(needle);
    return haystack.find(needle) != std::string::npos;
}

bool saveSettings(const RenderSettings& s, const std::string& path)
{
    std::ofstream out(path);
    if (!out)
    {
        return false;
    }
    out << "sunIntensity=" << s.sunIntensity << "\n";
    out << "sunAzimuth=" << s.sunAzimuth << "\n";
    out << "sunElevation=" << s.sunElevation << "\n";
    out << "envIntensity=" << s.envIntensity << "\n";
    out << "shadows=" << (s.shadows ? 1 : 0) << "\n";
    out << "exposure=" << s.exposure << "\n";
    out << "bloom=" << (s.bloom ? 1 : 0) << "\n";
    out << "bloomStrength=" << s.bloomStrength << "\n";
    out << "tonemapper=" << s.tonemapper << "\n";
    out << "ssao=" << (s.ssao ? 1 : 0) << "\n";
    out << "ssaoRadius=" << s.ssaoRadius << "\n";
    out << "ssaoIntensity=" << s.ssaoIntensity << "\n";
    out << "reflections=" << (s.reflections ? 1 : 0) << "\n";
    out << "reflectionStrength=" << s.reflectionStrength << "\n";
    out << "floorRoughness=" << s.floorRoughness << "\n";
    return static_cast<bool>(out);
}

bool loadSettings(RenderSettings& s, const std::string& path)
{
    std::ifstream in(path);
    if (!in)
    {
        return false;
    }

    std::string line;
    while (std::getline(in, line))
    {
        size_t eq = line.find('=');
        if (eq == std::string::npos)
        {
            continue;
        }
        std::string key = line.substr(0, eq);
        float value = 0.0f;
        try
        {
            value = std::stof(line.substr(eq + 1));
        }
        catch (...)
        {
            continue;
        }

        if (key == "sunIntensity") s.sunIntensity = value;
        else if (key == "sunAzimuth") s.sunAzimuth = value;
        else if (key == "sunElevation") s.sunElevation = value;
        else if (key == "envIntensity") s.envIntensity = value;
        else if (key == "shadows") s.shadows = value != 0.0f;
        else if (key == "exposure") s.exposure = value;
        else if (key == "bloom") s.bloom = value != 0.0f;
        else if (key == "bloomStrength") s.bloomStrength = value;
        else if (key == "tonemapper") s.tonemapper = std::clamp(static_cast<int>(value), 0, TONEMAPPER_COUNT - 1);
        else if (key == "ssao") s.ssao = value != 0.0f;
        else if (key == "ssaoRadius") s.ssaoRadius = value;
        else if (key == "ssaoIntensity") s.ssaoIntensity = value;
        else if (key == "reflections") s.reflections = value != 0.0f;
        else if (key == "reflectionStrength") s.reflectionStrength = value;
        else if (key == "floorRoughness") s.floorRoughness = value;
    }
    return true;
}

}

void BlackGlass::initImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.TabRounding = 4.0f;
    style.GrabRounding = 3.0f;

    const ImVec4 black(0.035f, 0.035f, 0.040f, 1.0f);
    const ImVec4 panel(0.075f, 0.075f, 0.082f, 1.0f);
    const ImVec4 raised(0.130f, 0.130f, 0.140f, 1.0f);
    const ImVec4 line(0.200f, 0.200f, 0.215f, 1.0f);
    const ImVec4 white(0.95f, 0.95f, 0.95f, 1.0f);
    const ImVec4 grey(0.52f, 0.52f, 0.55f, 1.0f);
    const ImVec4 red(0.82f, 0.09f, 0.11f, 1.0f);
    const ImVec4 redHover(0.95f, 0.20f, 0.22f, 1.0f);
    const ImVec4 redActive(0.60f, 0.05f, 0.07f, 1.0f);
    const ImVec4 redDim(0.82f, 0.09f, 0.11f, 0.35f);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = white;
    colors[ImGuiCol_TextDisabled] = grey;
    colors[ImGuiCol_WindowBg] = black;
    colors[ImGuiCol_ChildBg] = black;
    colors[ImGuiCol_PopupBg] = panel;
    colors[ImGuiCol_Border] = line;
    colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_FrameBg] = raised;
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.190f, 0.190f, 0.205f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = redDim;
    colors[ImGuiCol_TitleBg] = black;
    colors[ImGuiCol_TitleBgActive] = panel;
    colors[ImGuiCol_TitleBgCollapsed] = black;
    colors[ImGuiCol_MenuBarBg] = panel;
    colors[ImGuiCol_ScrollbarBg] = black;
    colors[ImGuiCol_ScrollbarGrab] = raised;
    colors[ImGuiCol_ScrollbarGrabHovered] = redHover;
    colors[ImGuiCol_ScrollbarGrabActive] = red;
    colors[ImGuiCol_CheckMark] = red;
    colors[ImGuiCol_SliderGrab] = red;
    colors[ImGuiCol_SliderGrabActive] = redHover;
    colors[ImGuiCol_Button] = raised;
    colors[ImGuiCol_ButtonHovered] = red;
    colors[ImGuiCol_ButtonActive] = redActive;
    colors[ImGuiCol_Header] = panel;
    colors[ImGuiCol_HeaderHovered] = redDim;
    colors[ImGuiCol_HeaderActive] = red;
    colors[ImGuiCol_Separator] = line;
    colors[ImGuiCol_SeparatorHovered] = redHover;
    colors[ImGuiCol_SeparatorActive] = red;
    colors[ImGuiCol_ResizeGrip] = line;
    colors[ImGuiCol_ResizeGripHovered] = redHover;
    colors[ImGuiCol_ResizeGripActive] = red;
    colors[ImGuiCol_Tab] = panel;
    colors[ImGuiCol_TabHovered] = redHover;
    colors[ImGuiCol_TabSelected] = red;
    colors[ImGuiCol_TabSelectedOverline] = white;
    colors[ImGuiCol_TabDimmed] = panel;
    colors[ImGuiCol_TabDimmedSelected] = redActive;
    colors[ImGuiCol_TabDimmedSelectedOverline] = grey;
    colors[ImGuiCol_PlotLines] = white;
    colors[ImGuiCol_PlotLinesHovered] = redHover;
    colors[ImGuiCol_PlotHistogram] = red;
    colors[ImGuiCol_PlotHistogramHovered] = redHover;
    colors[ImGuiCol_TextSelectedBg] = redDim;
    colors[ImGuiCol_NavHighlight] = red;
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.65f);

    for (int i = 0; i < ImGuiCol_COUNT; i++)
    {
        ImVec4& color = style.Colors[i];
        color.x = std::pow(color.x, 2.2f);
        color.y = std::pow(color.y, 2.2f);
        color.z = std::pow(color.z, 2.2f);
    }

    ImGui_ImplGlfw_InitForVulkan(m_window, true);

    m_uiColorFormat = M_SWAPCHAINIMAGEFORMAT;

    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_3;
    info.Instance = M_INSTANCE;
    info.PhysicalDevice = M_PHYSICALDEVICE;
    info.Device = M_DEVICE;
    info.QueueFamily = findQueueFamilies(M_PHYSICALDEVICE).graphicsFamily.value();
    info.Queue = M_GRAPHICSQUEUE;
    info.MinImageCount = 2;
    info.ImageCount = std::max<uint32_t>(2, static_cast<uint32_t>(M_SWAPCHAINIMAGES.size()));
    info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.DescriptorPoolSize = 16;
    info.UseDynamicRendering = true;
    info.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &m_uiColorFormat;
    info.CheckVkResultFn = checkVkResult;

    if (!ImGui_ImplVulkan_Init(&info))
    {
        throw std::runtime_error("Failed to initialize the ImGui Vulkan backend!");
    }

    glm::vec3 dir = glm::normalize(M_LIGHTDIR);
    m_settings.sunElevation = glm::degrees(std::asin(dir.y));
    m_settings.sunAzimuth = glm::degrees(std::atan2(dir.z, dir.x));
    m_defaultSettings = m_settings;

    scanAssets();

    initEditor();

    m_imguiReady = true;
    std::cout << "  [initImGui] SUCCESS - Dear ImGui ready.\n";
}

void BlackGlass::scanAssets()
{
    namespace fs = std::filesystem;
    m_assetFiles.clear();

    auto isModelFile = [](const fs::path& p)
    {
        std::string extension = p.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return extension == ".glb" || extension == ".gltf";
    };

    std::error_code error;
    fs::path root(BLACK_GLASS_ASSET_DIR);

    std::function<void(const fs::path&)> scanDirectory = [&](const fs::path& dir)
    {
        std::vector<fs::path> models;
        std::vector<fs::path> subdirs;
        std::error_code listError;
        for (fs::directory_iterator it(dir, listError), end; !listError && it != end; it.increment(listError))
        {
            if (it->is_directory())
            {
                subdirs.push_back(it->path());
            }
            else if (it->is_regular_file() && isModelFile(it->path()))
            {
                models.push_back(it->path());
            }
        }

        if (dir != root && models.size() > 1)
        {
            std::vector<fs::path> named;
            for (const fs::path& m : models)
            {
                if (m.stem() == dir.filename())
                {
                    named.push_back(m);
                }
            }
            if (!named.empty())
            {
                models = named;
            }
        }

        for (const fs::path& m : models)
        {
            m_assetFiles.push_back(fs::relative(m, root).generic_string());
        }

        if (dir == root || models.empty())
        {
            for (const fs::path& sub : subdirs)
            {
                scanDirectory(sub);
            }
        }
    };

    scanDirectory(root);
    std::sort(m_assetFiles.begin(), m_assetFiles.end());
}

void BlackGlass::shutdownImGui()
{
    if (!m_imguiReady)
    {
        return;
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    m_imguiReady = false;
}

bool BlackGlass::uiWantsMouse() const
{
    return m_imguiReady && (m_showUi || m_quitPrompt) && ImGui::GetIO().WantCaptureMouse;
}

bool BlackGlass::uiWantsKeyboard() const
{
    return m_imguiReady && (m_showUi || m_quitPrompt) && ImGui::GetIO().WantCaptureKeyboard;
}

void BlackGlass::toggleMouseCapture()
{
    setMouseCaptured(!m_mouseCaptured);
}

void BlackGlass::toggleUi()
{
    m_showUi = !m_showUi;
}

void BlackGlass::beginUiFrame()
{
    if (!m_imguiReady || (!m_showUi && !m_quitPrompt))
    {
        return;
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    buildUi();
    ImGui::Render();
}

void BlackGlass::requestQuit()
{
    if (!m_imguiReady)
    {
        glfwSetWindowShouldClose(m_window, GLFW_TRUE);
        return;
    }
    if (m_editor && m_editor->cancelTool())
    {
        return;
    }
    if (ImGui::IsAnyItemActive())
    {
        return;
    }
    setMouseCaptured(false);
    m_quitPrompt = true;
}

void BlackGlass::drawQuitPrompt()
{
    if (!m_quitPrompt)
    {
        return;
    }

    ImGui::OpenPopup("Quit Black Glass?");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Quit Black Glass?", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
    {
        ImGui::Text("Are you sure you want to quit?");
        ImGui::Spacing();
        if (ImGui::Button("Quit", ImVec2(120.0f, 0.0f)))
        {
            glfwSetWindowShouldClose(m_window, GLFW_TRUE);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
        {
            m_quitPrompt = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::TextDisabled("Esc: cancel");
        ImGui::EndPopup();
    }
}

void BlackGlass::applySunDirection()
{
    float azimuth = glm::radians(m_settings.sunAzimuth);
    float elevation = glm::radians(m_settings.sunElevation);
    M_LIGHTDIR = glm::vec3(std::cos(elevation) * std::cos(azimuth),
                           std::sin(elevation),
                           std::cos(elevation) * std::sin(azimuth));
    m_shadowDirty = true;
}

void BlackGlass::buildUi()
{
    if (!m_showUi)
    {
        drawQuitPrompt();
        return;
    }

    ImGuiIO& io = ImGui::GetIO();

    m_frameTimes[m_frameTimeOffset] = io.DeltaTime * 1000.0f;
    m_frameTimeOffset = (m_frameTimeOffset + 1) % m_frameTimes.size();

    auto tip = [](const char* text)
    {
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", text);
        }
    };

    auto applySun = [&]()
    {
        applySunDirection();
    };

    ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(440.0f, 640.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGui::Begin("Black Glass");

    ImGui::Text("%.1f FPS  (%.2f ms)", io.Framerate, 1000.0f / std::max(io.Framerate, 1.0f));
    ImGui::TextDisabled("%s", m_mouseCaptured ? "Tab: release mouse" : "Hold right mouse to fly, Tab to toggle");
    ImGui::TextDisabled("F1: hide UI    Esc: quit");

    if (ImGui::BeginTabBar("MainTabs"))
    {
        if (ImGui::BeginTabItem("Scene"))
        {
            if (ImGui::CollapsingHeader("Model library", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::TextDisabled("assets/");
                ImGui::SameLine();
                if (ImGui::SmallButton("Rescan"))
                {
                    scanAssets();
                }
                tip("Search src/assets for .glb and .gltf models, one folder per multi-file model");

                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint("##assetsearch", "Search models...", m_assetSearch, sizeof(m_assetSearch));

                int shown = 0;
                for (size_t i = 0; i < m_assetFiles.size(); i++)
                {
                    if (!matchesSearch(m_assetFiles[i], m_assetSearch))
                    {
                        continue;
                    }
                    shown++;

                    std::string fullPath = std::string(BLACK_GLASS_ASSET_DIR) + "/" + m_assetFiles[i];
                    bool loaded = false;
                    for (const auto& asset : M_LIBRARY)
                    {
                        if (asset->path == fullPath)
                        {
                            loaded = true;
                            break;
                        }
                    }

                    ImGui::PushID(static_cast<int>(i));
                    ImGui::Text("%s", m_assetFiles[i].c_str());
                    ImGui::SameLine();
                    if (loaded)
                    {
                        ImGui::TextDisabled("loaded");
                    }
                    else if (ImGui::Button("Import"))
                    {
                        importAsset(fullPath);
                    }
                    ImGui::PopID();
                }
                if (m_assetFiles.empty())
                {
                    ImGui::TextDisabled("No .glb or .gltf files found");
                }
                else if (shown == 0)
                {
                    ImGui::TextDisabled("No models match the search");
                }
                if (!m_loadError.empty())
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 380.0f);
                    ImGui::TextWrapped("%s", m_loadError.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::PopStyleColor();
                }

                ImGui::Separator();

                size_t unloadIndex = M_LIBRARY.size();
                int placeIndex = -1;
                int addIndex = -1;
                for (size_t i = 0; i < M_LIBRARY.size(); i++)
                {
                    if (M_LIBRARY[i]->brush || M_LIBRARY[i]->marker)
                    {
                        continue;
                    }

                    int instances = 0;
                    for (const auto& model : M_MODELS)
                    {
                        if (model->asset == M_LIBRARY[i] && !model->ghost)
                        {
                            instances++;
                        }
                    }

                    ImGui::PushID(static_cast<int>(i));
                    ImGui::Text("%s  (%d placed)", M_LIBRARY[i]->name.c_str(), instances);
                    ImGui::SameLine();
                    bool placing = m_placingAsset == static_cast<int>(i);
                    if (ImGui::Button(placing ? "Cancel" : "Place"))
                    {
                        if (placing)
                        {
                            cancelPlacement();
                        }
                        else
                        {
                            placeIndex = static_cast<int>(i);
                        }
                    }
                    tip("Click the floor to place an instance");
                    ImGui::SameLine();
                    if (ImGui::Button("Add"))
                    {
                        addIndex = static_cast<int>(i);
                    }
                    tip("Add an instance automatically");
                    ImGui::SameLine();
                    if (ImGui::Button("Unload"))
                    {
                        unloadIndex = i;
                    }
                    tip("Free this model and remove all its instances");
                    ImGui::PopID();
                }

                if (m_ghost)
                {
                    ImGui::TextDisabled("Click the floor to place, Shift-click to keep placing");
                    ImGui::TextDisabled("Scroll to rotate, Delete to cancel");
                }

                if (placeIndex >= 0)
                {
                    beginPlacement(placeIndex);
                }
                if (addIndex >= 0)
                {
                    selectOnly(addInstance(addIndex, true));
                }
                if (unloadIndex < M_LIBRARY.size())
                {
                    removeAsset(unloadIndex);
                }
            }

            if (ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::TextDisabled("Click a model to select, drag to move, scroll to rotate");

                size_t removeIndex = M_MODELS.size();
                for (size_t i = 0; i < M_MODELS.size(); i++)
                {
                    Model& model = *M_MODELS[i];
                    if (model.ghost)
                    {
                        continue;
                    }

                    ImGui::PushID(static_cast<int>(i));
                    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen;
                    if (isSelected(&model))
                    {
                        flags |= ImGuiTreeNodeFlags_Selected;
                    }
                    bool open = ImGui::TreeNodeEx(model.name.c_str(), flags);
                    if (ImGui::IsItemClicked())
                    {
                        if (ImGui::GetIO().KeyShift)
                        {
                            toggleSelection(&model);
                        }
                        else
                        {
                            selectOnly(&model);
                        }
                    }
                    if (open)
                    {
                        bool changed = false;
                        changed |= ImGui::Checkbox("Visible", &model.visible);
                        changed |= ImGui::DragFloat3("Position", &model.position.x, 0.02f);
                        changed |= ImGui::SliderFloat("Yaw", &model.yawDegrees, -180.0f, 180.0f, "%.0f deg");
                        changed |= ImGui::SliderFloat("Scale", &model.scale, 0.001f, 100.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
                        tip("Uniform scale applied on top of the model's native size");

                        glm::vec3 size = model.asset->localMax - model.asset->localMin;
                        if (model.brush)
                        {
                            size *= model.brush->size;
                        }
                        float longest = std::max(size.x, std::max(size.y, size.z));
                        ImGui::TextDisabled("Native size  %.2f x %.2f x %.2f", size.x, size.y, size.z);

                        ImGui::SetNextItemWidth(90.0f);
                        ImGui::DragFloat("##fitsize", &m_fitSize, 0.1f, 0.1f, 10000.0f, "%.1f");
                        tip("Target length of the model's longest side");
                        ImGui::SameLine();
                        if (ImGui::Button("Fit to size") && longest > 1e-6f)
                        {
                            model.scale = m_fitSize / longest;
                            changed = true;
                        }
                        tip("Scale so the longest side matches the target length");
                        ImGui::SameLine();
                        if (ImGui::Button("Reset scale"))
                        {
                            model.scale = 1.0f;
                            changed = true;
                        }

                        if (ImGui::Button("Remove"))
                        {
                            removeIndex = i;
                        }
                        if (changed)
                        {
                            m_shadowDirty = true;
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }

                if (ImGui::Button("Frame scene"))
                {
                    frameScene();
                }
                tip("Move the camera to see every model");

                if (removeIndex < M_MODELS.size())
                {
                    removeModel(removeIndex);
                }
            }

            if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::SliderFloat("Field of view", &m_fov, 20.0f, 90.0f, "%.0f deg");
                ImGui::SliderFloat("Move speed", &m_camera.speed, 0.1f, 20.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
                tip("Fly speed while holding the right mouse button");
                ImGui::Text("Position  %.2f  %.2f  %.2f", m_camera.position.x, m_camera.position.y, m_camera.position.z);
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Editor"))
        {
            if (m_editor)
            {
                m_editor->drawPanel();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Rendering"))
        {
            if (ImGui::Button("Save settings"))
            {
                m_settingsStatus = saveSettings(m_settings, SETTINGS_FILE)
                    ? std::string("Saved to ") + SETTINGS_FILE
                    : std::string("Could not write ") + SETTINGS_FILE;
            }
            tip("Write all rendering settings to black_glass_settings.cfg in the working directory");
            ImGui::SameLine();
            if (ImGui::Button("Load settings"))
            {
                if (loadSettings(m_settings, SETTINGS_FILE))
                {
                    applySun();
                    m_settingsStatus = std::string("Loaded ") + SETTINGS_FILE;
                }
                else
                {
                    m_settingsStatus = std::string("Could not read ") + SETTINGS_FILE;
                }
            }
            tip("Restore settings saved earlier");
            ImGui::SameLine();
            if (ImGui::Button("Reset all"))
            {
                m_settings = m_defaultSettings;
                applySun();
                m_settingsStatus = "Reset to defaults";
            }
            tip("Restore every rendering setting to its startup value");
            if (!m_settingsStatus.empty())
            {
                ImGui::TextDisabled("%s", m_settingsStatus.c_str());
            }

            if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen))
            {
                bool lightChanged = false;
                lightChanged |= ImGui::SliderFloat("Sun azimuth", &m_settings.sunAzimuth, -180.0f, 180.0f, "%.0f deg");
                lightChanged |= ImGui::SliderFloat("Sun elevation", &m_settings.sunElevation, 5.0f, 90.0f, "%.0f deg");
                ImGui::SliderFloat("Sun intensity", &m_settings.sunIntensity, 0.0f, 10.0f, "%.2f");
                ImGui::SliderFloat("Environment", &m_settings.envIntensity, 0.0f, 4.0f, "%.2f");
                tip("Brightness of image-based lighting and the sky");
                ImGui::Checkbox("Shadows", &m_settings.shadows);
                if (M_IBLFROMFILE)
                {
                    ImGui::TextDisabled("Sun is off while an HDR environment is loaded");
                }
                if (ImGui::SmallButton("Reset##lighting"))
                {
                    m_settings.sunIntensity = m_defaultSettings.sunIntensity;
                    m_settings.sunAzimuth = m_defaultSettings.sunAzimuth;
                    m_settings.sunElevation = m_defaultSettings.sunElevation;
                    m_settings.envIntensity = m_defaultSettings.envIntensity;
                    m_settings.shadows = m_defaultSettings.shadows;
                    lightChanged = true;
                }

                if (lightChanged)
                {
                    applySun();
                }
            }

            if (ImGui::CollapsingHeader("Effects", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Checkbox("SSAO", &m_settings.ssao);
                tip("Screen-space ambient occlusion, costs a depth prepass and a half-resolution pass");
                ImGui::SliderFloat("SSAO radius", &m_settings.ssaoRadius, 0.1f, 3.0f, "%.2f");
                tip("Sampling radius relative to the scene size");
                ImGui::SliderFloat("SSAO intensity", &m_settings.ssaoIntensity, 0.2f, 3.0f, "%.2f");
                tip("Contrast of the occlusion term");
                ImGui::Checkbox("Floor reflections", &m_settings.reflections);
                tip("Render the scene mirrored in the floor, costs an extra scene pass");
                ImGui::SliderFloat("Reflection strength", &m_settings.reflectionStrength, 0.0f, 6.0f, "%.2f");
                ImGui::SliderFloat("Floor roughness", &m_settings.floorRoughness, 0.05f, 1.0f, "%.2f");
                tip("Lower values give sharper reflections");
                if (ImGui::SmallButton("Reset##effects"))
                {
                    m_settings.ssao = m_defaultSettings.ssao;
                    m_settings.ssaoRadius = m_defaultSettings.ssaoRadius;
                    m_settings.ssaoIntensity = m_defaultSettings.ssaoIntensity;
                    m_settings.reflections = m_defaultSettings.reflections;
                    m_settings.reflectionStrength = m_defaultSettings.reflectionStrength;
                    m_settings.floorRoughness = m_defaultSettings.floorRoughness;
                }
            }

            if (ImGui::CollapsingHeader("Post-processing", ImGuiTreeNodeFlags_DefaultOpen))
            {
                float stops = std::log2(m_settings.exposure);
                if (ImGui::SliderFloat("Exposure (EV)", &stops, -4.0f, 4.0f, "%.2f"))
                {
                    m_settings.exposure = std::exp2(stops);
                }
                tip("Exposure in stops, 0 is neutral");
                ImGui::Combo("Tonemapper", &m_settings.tonemapper, TONEMAPPER_NAMES, TONEMAPPER_COUNT);
                ImGui::Checkbox("Bloom", &m_settings.bloom);
                ImGui::SliderFloat("Bloom strength", &m_settings.bloomStrength, 0.0f, 0.3f, "%.3f");
                if (ImGui::SmallButton("Reset##post"))
                {
                    m_settings.exposure = m_defaultSettings.exposure;
                    m_settings.tonemapper = m_defaultSettings.tonemapper;
                    m_settings.bloom = m_defaultSettings.bloom;
                    m_settings.bloomStrength = m_defaultSettings.bloomStrength;
                }
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Debug"))
        {
            if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen))
            {
                float peak = 16.7f;
                for (float ms : m_frameTimes)
                {
                    peak = std::max(peak, ms);
                }
                peak = std::ceil(peak / 5.0f) * 5.0f;

                char overlay[32];
                std::snprintf(overlay, sizeof(overlay), "%.2f ms", io.DeltaTime * 1000.0f);
                ImGui::PlotLines("##frametimes", m_frameTimes.data(), static_cast<int>(m_frameTimes.size()),
                                 static_cast<int>(m_frameTimeOffset), overlay, 0.0f, peak, ImVec2(-1.0f, 60.0f));
                tip("Frame time over the last 120 frames");

                if (!m_timestampsSupported)
                {
                    ImGui::TextDisabled("GPU timings are not supported on this device");
                }
                else
                {
                    if (ImGui::Checkbox("Measure GPU stages", &m_gpuTimingsEnabled))
                    {
                        for (float& ms : m_gpuSegmentsMs)
                        {
                            ms = 0.0f;
                        }
                        for (bool& recorded : m_timestampRecorded)
                        {
                            recorded = false;
                        }
                    }
                    tip("Timestamp queries can cost frame time on some GPUs, leave off unless measuring");
                }

                if (m_timestampsSupported && m_gpuTimingsEnabled)
                {
                    static const char* const names[GPU_SEGMENTS] = {
                        "SSAO", "Reflections", "Main pass", "Glass", "Bloom", "Composite + UI"
                    };

                    float total = 0.0f;
                    for (uint32_t i = 0; i < GPU_SEGMENTS; i++)
                    {
                        total += m_gpuSegmentsMs[i];
                    }
                    ImGui::Text("GPU  %.2f ms", total);
                    tip("Smoothed GPU time per stage, a stage reads 0 when it is switched off");

                    for (uint32_t i = 0; i < GPU_SEGMENTS; i++)
                    {
                        char label[64];
                        std::snprintf(label, sizeof(label), "%s  %.2f ms", names[i], m_gpuSegmentsMs[i]);
                        ImGui::ProgressBar(total > 0.0f ? m_gpuSegmentsMs[i] / total : 0.0f, ImVec2(-1.0f, 0.0f), label);
                    }
                }
            }

            if (ImGui::CollapsingHeader("Debug view", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Combo("View", &m_debugView, DEBUG_VIEW_NAMES, IM_ARRAYSIZE(DEBUG_VIEW_NAMES));
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();

    if (m_editor)
    {
        m_editor->drawOverlayUi();
    }

    drawQuitPrompt();
}

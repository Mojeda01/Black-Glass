#include "../BlackGlass.h"
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
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

    scanAssets();

    m_imguiReady = true;
    std::cout << "  [initImGui] SUCCESS - Dear ImGui ready.\n";
}

void BlackGlass::scanAssets()
{
    namespace fs = std::filesystem;
    m_assetFiles.clear();

    std::error_code error;
    fs::path root(BLACK_GLASS_ASSET_DIR);
    for (fs::recursive_directory_iterator it(root, error), end; !error && it != end; it.increment(error))
    {
        if (!it->is_regular_file())
        {
            continue;
        }
        std::string extension = it->path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension == ".glb" || extension == ".gltf")
        {
            m_assetFiles.push_back(fs::relative(it->path(), root).generic_string());
        }
    }
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
    return m_imguiReady && m_showUi && ImGui::GetIO().WantCaptureMouse;
}

bool BlackGlass::uiWantsKeyboard() const
{
    return m_imguiReady && m_showUi && ImGui::GetIO().WantCaptureKeyboard;
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
    if (!m_imguiReady || !m_showUi)
    {
        return;
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    buildUi();
    ImGui::Render();
}

void BlackGlass::buildUi()
{
    ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGui::Begin("Black Glass", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::Text("%.1f FPS  (%.2f ms)", io.Framerate, 1000.0f / std::max(io.Framerate, 1.0f));
    ImGui::TextDisabled("%s", m_mouseCaptured ? "Tab: release mouse" : "Hold right mouse to fly, Tab to toggle");
    ImGui::TextDisabled("F1: hide UI    Esc: quit");

    if (ImGui::CollapsingHeader("Model library", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextDisabled("assets/");
        ImGui::SameLine();
        if (ImGui::SmallButton("Rescan"))
        {
            scanAssets();
        }

        for (size_t i = 0; i < m_assetFiles.size(); i++)
        {
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
        if (!m_loadError.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 380.0f);
            ImGui::TextWrapped("%s", m_loadError.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }

        size_t unloadIndex = M_LIBRARY.size();
        int placeIndex = -1;
        int addIndex = -1;
        for (size_t i = 0; i < M_LIBRARY.size(); i++)
        {
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
            ImGui::SameLine();
            if (ImGui::Button("Add"))
            {
                addIndex = static_cast<int>(i);
            }
            ImGui::SameLine();
            if (ImGui::Button("Unload"))
            {
                unloadIndex = i;
            }
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
            m_selected = addInstance(addIndex, true);
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
            if (&model == m_selected)
            {
                flags |= ImGuiTreeNodeFlags_Selected;
            }
            bool open = ImGui::TreeNodeEx(model.name.c_str(), flags);
            if (ImGui::IsItemClicked())
            {
                m_selected = &model;
            }
            if (open)
            {
                bool changed = false;
                changed |= ImGui::Checkbox("Visible", &model.visible);
                changed |= ImGui::DragFloat3("Position", &model.position.x, 0.02f);
                changed |= ImGui::SliderFloat("Yaw", &model.yawDegrees, -180.0f, 180.0f, "%.0f deg");
                changed |= ImGui::SliderFloat("Scale", &model.scale, 0.01f, 20.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
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

        if (removeIndex < M_MODELS.size())
        {
            removeModel(removeIndex);
        }
    }

    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SliderFloat("Field of view", &m_fov, 20.0f, 90.0f, "%.0f deg");
        ImGui::SliderFloat("Move speed", &m_camera.speed, 0.1f, 20.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
        ImGui::Text("Position  %.2f  %.2f  %.2f", m_camera.position.x, m_camera.position.y, m_camera.position.z);
    }

    if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen))
    {
        bool lightChanged = false;
        lightChanged |= ImGui::SliderFloat("Sun azimuth", &m_settings.sunAzimuth, -180.0f, 180.0f, "%.0f deg");
        lightChanged |= ImGui::SliderFloat("Sun elevation", &m_settings.sunElevation, 5.0f, 90.0f, "%.0f deg");
        ImGui::SliderFloat("Sun intensity", &m_settings.sunIntensity, 0.0f, 10.0f, "%.2f");
        ImGui::SliderFloat("Environment", &m_settings.envIntensity, 0.0f, 4.0f, "%.2f");
        ImGui::Checkbox("Shadows", &m_settings.shadows);
        if (M_IBLFROMFILE)
        {
            ImGui::TextDisabled("Sun is off while an HDR environment is loaded");
        }

        if (lightChanged)
        {
            float azimuth = glm::radians(m_settings.sunAzimuth);
            float elevation = glm::radians(m_settings.sunElevation);
            M_LIGHTDIR = glm::vec3(std::cos(elevation) * std::cos(azimuth),
                                   std::sin(elevation),
                                   std::cos(elevation) * std::sin(azimuth));
            m_shadowDirty = true;
        }
    }

    if (ImGui::CollapsingHeader("Effects", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Checkbox("SSAO", &m_settings.ssao);
        ImGui::SliderFloat("SSAO radius", &m_settings.ssaoRadius, 0.1f, 3.0f, "%.2f");
        ImGui::SliderFloat("SSAO intensity", &m_settings.ssaoIntensity, 0.2f, 3.0f, "%.2f");
        ImGui::Checkbox("Floor reflections", &m_settings.reflections);
        ImGui::SliderFloat("Reflection strength", &m_settings.reflectionStrength, 0.0f, 6.0f, "%.2f");
        ImGui::SliderFloat("Floor roughness", &m_settings.floorRoughness, 0.05f, 1.0f, "%.2f");
    }

    if (ImGui::CollapsingHeader("Post-processing", ImGuiTreeNodeFlags_DefaultOpen))
    {
        float stops = std::log2(m_settings.exposure);
        if (ImGui::SliderFloat("Exposure (EV)", &stops, -4.0f, 4.0f, "%.2f"))
        {
            m_settings.exposure = std::exp2(stops);
        }
        ImGui::Combo("Tonemapper", &m_settings.tonemapper, TONEMAPPER_NAMES, TONEMAPPER_COUNT);
        ImGui::Checkbox("Bloom", &m_settings.bloom);
        ImGui::SliderFloat("Bloom strength", &m_settings.bloomStrength, 0.0f, 0.3f, "%.3f");
    }

    if (ImGui::CollapsingHeader("Debug view", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Combo("View", &m_debugView, DEBUG_VIEW_NAMES, IM_ARRAYSIZE(DEBUG_VIEW_NAMES));
    }

    ImGui::End();
}

#include "../AlethiaV3.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstdio>
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

void AlethiaV3::initImGui()
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
    m_sunElevation = glm::degrees(std::asin(dir.y));
    m_sunAzimuth = glm::degrees(std::atan2(dir.z, dir.x));

    std::string defaultPath = std::string(ALETHIA_V3_ASSET_DIR) + "/BoxTextured.glb";
    std::snprintf(m_modelPathInput, sizeof(m_modelPathInput), "%s", defaultPath.c_str());

    m_imguiReady = true;
    std::cout << "  [initImGui] SUCCESS - Dear ImGui ready.\n";
}

void AlethiaV3::shutdownImGui()
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

bool AlethiaV3::uiWantsMouse() const
{
    return m_imguiReady && m_showUi && ImGui::GetIO().WantCaptureMouse;
}

bool AlethiaV3::uiWantsKeyboard() const
{
    return m_imguiReady && m_showUi && ImGui::GetIO().WantCaptureKeyboard;
}

void AlethiaV3::toggleMouseCapture()
{
    setMouseCaptured(!m_mouseCaptured);
}

void AlethiaV3::toggleUi()
{
    m_showUi = !m_showUi;
}

void AlethiaV3::beginUiFrame()
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

void AlethiaV3::buildUi()
{
    ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGui::Begin("Black Glass", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::Text("%.1f FPS  (%.2f ms)", io.Framerate, 1000.0f / std::max(io.Framerate, 1.0f));
    ImGui::TextDisabled("%s", m_mouseCaptured ? "Tab: release mouse" : "Click the viewport to fly, Tab to release");
    ImGui::TextDisabled("F1: hide UI    Esc: quit");

    if (ImGui::CollapsingHeader("Models", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SetNextItemWidth(320.0f);
        ImGui::InputText("##modelPath", m_modelPathInput, sizeof(m_modelPathInput));
        ImGui::SameLine();
        if (ImGui::Button("Load"))
        {
            loadModel(m_modelPathInput);
        }
        if (!m_loadError.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 380.0f);
            ImGui::TextWrapped("%s", m_loadError.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }

        size_t removeIndex = M_MODELS.size();
        for (size_t i = 0; i < M_MODELS.size(); i++)
        {
            Model& model = *M_MODELS[i];
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::TreeNodeEx(model.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
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
        lightChanged |= ImGui::SliderFloat("Sun azimuth", &m_sunAzimuth, -180.0f, 180.0f, "%.0f deg");
        lightChanged |= ImGui::SliderFloat("Sun elevation", &m_sunElevation, 5.0f, 90.0f, "%.0f deg");
        ImGui::SliderFloat("Sun intensity", &m_sunIntensity, 0.0f, 10.0f, "%.2f");
        ImGui::SliderFloat("Environment", &m_envIntensity, 0.0f, 4.0f, "%.2f");
        ImGui::Checkbox("Shadows", &m_shadowsEnabled);
        if (M_IBLFROMFILE)
        {
            ImGui::TextDisabled("Sun is off while an HDR environment is loaded");
        }

        if (lightChanged)
        {
            float azimuth = glm::radians(m_sunAzimuth);
            float elevation = glm::radians(m_sunElevation);
            M_LIGHTDIR = glm::vec3(std::cos(elevation) * std::cos(azimuth),
                                   std::sin(elevation),
                                   std::cos(elevation) * std::sin(azimuth));
            m_shadowDirty = true;
        }
    }

    if (ImGui::CollapsingHeader("Post-processing", ImGuiTreeNodeFlags_DefaultOpen))
    {
        float stops = std::log2(m_exposure);
        if (ImGui::SliderFloat("Exposure (EV)", &stops, -4.0f, 4.0f, "%.2f"))
        {
            m_exposure = std::exp2(stops);
        }
        ImGui::Checkbox("Bloom", &m_bloomEnabled);
        ImGui::SliderFloat("Bloom strength", &m_bloomStrength, 0.0f, 0.3f, "%.3f");
    }

    if (ImGui::CollapsingHeader("Debug view", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Combo("View", &m_debugView, DEBUG_VIEW_NAMES, IM_ARRAYSIZE(DEBUG_VIEW_NAMES));
    }

    ImGui::End();
}

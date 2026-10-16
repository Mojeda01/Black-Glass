#include "../BlackGlass.h"
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <chrono>
#include <algorithm>

#include "../imgui/imgui.h"

#include <glm/gtc/matrix_transform.hpp>

void BlackGlass::createDescriptorSetLayout()
{
    std::array<VkDescriptorSetLayoutBinding, GRAPHICS_BINDING_COUNT> bindings{};

    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    for (uint32_t slot = 0; slot < MATERIAL_TEXTURE_SLOTS; slot++)
    {
        bindings[2 + slot].binding = 2 + slot;
        bindings[2 + slot].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[2 + slot].descriptorCount = 1;
        bindings[2 + slot].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    for (uint32_t i = 0; i < IBL_TEXTURE_BINDINGS; i++)
    {
        uint32_t index = 2 + MATERIAL_TEXTURE_SLOTS + i;
        bindings[index].binding = index;
        bindings[index].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[index].descriptorCount = 1;
        bindings[index].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    bindings[SHADOW_BINDING_INDEX].binding = SHADOW_BINDING_INDEX;
    bindings[SHADOW_BINDING_INDEX].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[SHADOW_BINDING_INDEX].descriptorCount = 1;
    bindings[SHADOW_BINDING_INDEX].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[SCENE_COLOR_BINDING_INDEX].binding = SCENE_COLOR_BINDING_INDEX;
    bindings[SCENE_COLOR_BINDING_INDEX].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[SCENE_COLOR_BINDING_INDEX].descriptorCount = 1;
    bindings[SCENE_COLOR_BINDING_INDEX].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    for (uint32_t index : { AO_BINDING_INDEX, REFLECTION_BINDING_INDEX })
    {
        bindings[index].binding = index;
        bindings[index].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[index].descriptorCount = 1;
        bindings[index].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size()); 
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(M_DEVICE, &layoutInfo, nullptr, &M_DESCRIPTORSETLAYOUT) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create descriptor set layout!");
    }

    M_PUSHDESCRIPTORFN = reinterpret_cast<PFN_vkCmdPushDescriptorSetKHR>(
        vkGetDeviceProcAddr(M_DEVICE, "vkCmdPushDescriptorSetKHR")
    );

    if (!M_PUSHDESCRIPTORFN)
    {
        throw std::runtime_error("Failed to load vkCmdPushDescriptorSetKHR!");
    }

    std::cout << "  [createDescriptorSetLayout] SUCCESS - Push descriptor layout created.\n";
}

void BlackGlass::createUniformBuffers()
{
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);
    M_UNIFORMBUFFERS.resize(MAX_FRAMES_IN_FLIGHT);
    M_UNIFORMBUFFERSMAPPED.resize(MAX_FRAMES_IN_FLIGHT);
    M_REFLUNIFORMBUFFERS.resize(MAX_FRAMES_IN_FLIGHT);
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        M_REFLUNIFORMBUFFERS[i] = createBuffer(
            bufferSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );

        M_UNIFORMBUFFERS[i] = createBuffer(
            bufferSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );

        M_UNIFORMBUFFERSMAPPED[i] = M_UNIFORMBUFFERS[i].mapped;
    }
    std::cout << "  [createUniformBuffers] SUCCESS - " << MAX_FRAMES_IN_FLIGHT
                    << " uniform buffers mapped.\n";
}

void BlackGlass::setMouseCaptured(bool captured)
{
    m_mouseCaptured = captured;
    glfwSetInputMode(m_window, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (captured)
    {
        m_firstMouse = true;
        if (m_imguiReady)
        {
            ImGui::SetWindowFocus(nullptr);
        }
    }
}

void BlackGlass::onMouseMove(double xpos, double ypos)
{
    if (!m_mouseCaptured)
    {
        return;
    }

    if (m_firstMouse)
    {
        m_lastX = xpos;
        m_lastY = ypos;
        m_firstMouse = false;
        return;
    }

    float deltaX = static_cast<float>(xpos - m_lastX);
    float deltaY = static_cast<float>(ypos - m_lastY);
    m_lastX = xpos;
    m_lastY = ypos;

    if (m_orthoMode != 0)
    {
        panOrtho(deltaX, deltaY);
        return;
    }

    m_camera.processMouse(deltaX, deltaY);
}

void BlackGlass::updateUniformBuffer(uint32_t currentImage)
{
    auto currentTime = std::chrono::high_resolution_clock::now(); 
    static auto startTime = currentTime;
    float time = std::chrono::duration<float>(currentTime - startTime).count();

    m_deltaTime = std::min(time - m_lastFrameTime, 0.1f);
    m_lastFrameTime = time;

    if (m_walkMode)
    {
        if (m_mouseCaptured && !m_quitPrompt)
        {
            updateWalk(m_deltaTime);
        }
    }
    else if (m_mouseCaptured && !m_quitPrompt && !(m_imguiReady && ImGui::GetIO().WantTextInput))
    {
        if (m_orthoMode != 0)
        {
            keyboardOrtho(m_deltaTime);
        }
        else
        {
            m_camera.processKeyboard(m_window, m_deltaTime);
        }
    }

    UniformBufferObject ubo{};

    ubo.view = m_camera.view();

    ubo.proj = glm::perspective(glm::radians(activeFov()),
                                M_SWAPCHAINEXTENT.width / static_cast<float>(M_SWAPCHAINEXTENT.height),
                                0.1f, 1000.0f);

    ubo.proj[1][1] *= -1;

    m_viewProj = ubo.proj * ubo.view;
    ubo.invViewProj = glm::inverse(m_viewProj);

    ubo.lightViewProj = M_LIGHTVIEWPROJ;
    ubo.lightDir = glm::vec4(glm::normalize(M_LIGHTDIR), m_settings.sunIntensity);
    ubo.shadowParams = glm::vec4(1.0f / static_cast<float>(SHADOW_MAP_SIZE), M_SHADOWNORMALBIAS, m_settings.shadows ? 1.0f : 0.0f, 0.0f);
    ubo.groundParams = M_GROUNDPARAMS;
    ubo.groundExtent = M_GROUNDEXTENT;
    for (uint32_t i = 0; i < GROUND_BOX_MAX; i++)
    {
        ubo.groundBoxes[i] = M_GROUNDBOXES[i];
    }

    ubo.cameraPos = glm::vec4(m_camera.position, 1.0f);
    ubo.debugParams = glm::vec4(static_cast<float>(m_debugView),
                                static_cast<float>(M_SCENECOPYMIPS - 1),
                                static_cast<float>(M_SWAPCHAINEXTENT.width),
                                static_cast<float>(M_SWAPCHAINEXTENT.height));
    ubo.iblParams = glm::vec4(M_IBLMAXMIP, m_settings.envIntensity, M_IBLFROMFILE ? 0.0f : 1.0f, 0.0f);

    float groundY = M_GROUNDEXTENT.z;
    m_ssaoActive = m_settings.ssao && M_PREPASSDEPTH.image != VK_NULL_HANDLE;
    m_reflectionActive = m_settings.reflections && m_debugView == 0 && M_GROUND != nullptr
                            && M_REFLCOLOR.image != VK_NULL_HANDLE
                            && m_camera.position.y > groundY + 0.01f;

    glm::vec3 sceneExtent = M_SCENEMAX - M_SCENEMIN;
    m_ssaoWorldRadius = std::max(m_settings.ssaoRadius * 0.1f * glm::length(sceneExtent), 0.001f);

    ubo.effectParams = glm::vec4(m_ssaoActive ? 1.0f : 0.0f, 0.0f,
                                 m_reflectionActive ? m_settings.reflectionStrength : 0.0f,
                                 m_settings.floorRoughness);
    ubo.passParams = glm::vec4(0.0f);
    ubo.projParams = glm::vec4(ubo.proj[2][2], ubo.proj[3][2], ubo.proj[0][0], ubo.proj[1][1]);

    std::vector<std::pair<float, const Model*>> activeLights;
    for (const auto& model : M_MODELS)
    {
        if (model->light && !model->ghost && !model->hidden && model->light->enabled)
        {
            glm::vec3 center = (model->worldMin + model->worldMax) * 0.5f;
            activeLights.emplace_back(glm::distance(center, m_camera.position), model.get());
        }
    }
    std::sort(activeLights.begin(), activeLights.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    uint32_t lightCount = static_cast<uint32_t>(std::min<size_t>(activeLights.size(), MAX_POINT_LIGHTS));
    for (uint32_t i = 0; i < lightCount; i++)
    {
        const Model* lightModel = activeLights[i].second;
        glm::vec3 center = (lightModel->worldMin + lightModel->worldMax) * 0.5f;
        ubo.lightPositions[i] = glm::vec4(center, lightModel->light->radius);
        ubo.lightColors[i] = glm::vec4(lightModel->light->color * lightModel->light->intensity, 0.0f);

        const LightDef& def = *lightModel->light;
        if (def.spot)
        {
            float yaw = glm::radians(lightModel->yawDegrees);
            float pitch = glm::radians(def.pitch);
            glm::vec3 direction(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch));
            float outer = std::clamp(def.outerAngle, 1.0f, 89.0f);
            float inner = std::clamp(def.innerAngle, 0.0f, outer - 0.5f);
            ubo.lightDirections[i] = glm::vec4(glm::normalize(direction), std::cos(glm::radians(outer)));
            ubo.lightColors[i].w = std::cos(glm::radians(inner));
        }
        else
        {
            ubo.lightDirections[i] = glm::vec4(0.0f, -1.0f, 0.0f, -2.0f);
        }
    }
    ubo.lightInfo = glm::vec4(static_cast<float>(lightCount), 0.0f, 0.0f, 0.0f);

    std::memcpy(M_UNIFORMBUFFERSMAPPED[currentImage], &ubo, sizeof(ubo));

    UniformBufferObject reflUbo = ubo;
    glm::mat4 mirror = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, groundY, 0.0f))
                     * glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, -1.0f, 1.0f))
                     * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -groundY, 0.0f));
    reflUbo.view = ubo.view * mirror;
    m_reflViewProj = ubo.proj * reflUbo.view;
    reflUbo.invViewProj = glm::inverse(m_reflViewProj);
    reflUbo.cameraPos = glm::vec4(glm::vec3(mirror * glm::vec4(m_camera.position, 1.0f)), 1.0f);
    reflUbo.effectParams = glm::vec4(0.0f, 0.0f, 0.0f, m_settings.floorRoughness);
    reflUbo.passParams = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    std::memcpy(M_REFLUNIFORMBUFFERS[currentImage].mapped, &reflUbo, sizeof(reflUbo));
}



#include "../AlethiaV3.h"
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <chrono>
#include <algorithm>

#include <glm/gtc/matrix_transform.hpp>

void AlethiaV3::createDescriptorSetLayout()
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

void AlethiaV3::createUniformBuffers()
{
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);
    M_UNIFORMBUFFERS.resize(MAX_FRAMES_IN_FLIGHT);
    M_UNIFORMBUFFERSMAPPED.resize(MAX_FRAMES_IN_FLIGHT);
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        M_UNIFORMBUFFERS[i] = createBuffer(
            bufferSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );

        vkMapMemory(M_DEVICE, M_UNIFORMBUFFERS[i].memory, 0, bufferSize, 0,
                        &M_UNIFORMBUFFERSMAPPED[i]);
    }
    std::cout << "  [createUniformBuffers] SUCCESS - " << MAX_FRAMES_IN_FLIGHT
                    << " uniform buffers mapped.\n";
}

void AlethiaV3::setMouseCaptured(bool captured)
{
    m_mouseCaptured = captured;
    glfwSetInputMode(m_window, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (captured)
    {
        m_firstMouse = true;
    }
}

void AlethiaV3::onMouseMove(double xpos, double ypos)
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

    m_camera.processMouse(deltaX, deltaY);
}

void AlethiaV3::updateUniformBuffer(uint32_t currentImage)
{
    auto currentTime = std::chrono::high_resolution_clock::now(); 
    static auto startTime = currentTime;
    float time = std::chrono::duration<float>(currentTime - startTime).count();

    m_deltaTime = std::min(time - m_lastFrameTime, 0.1f);
    m_lastFrameTime = time;

    if (m_mouseCaptured)
    {
        m_camera.processKeyboard(m_window, m_deltaTime);
    }

    UniformBufferObject ubo{};

    ubo.view = m_camera.view();

    ubo.proj = glm::perspective(glm::radians(45.0f),
                                M_SWAPCHAINEXTENT.width / static_cast<float>(M_SWAPCHAINEXTENT.height),
                                0.1f, 1000.0f);

    ubo.proj[1][1] *= -1;

    ubo.invViewProj = glm::inverse(ubo.proj * ubo.view);

    ubo.lightViewProj = M_LIGHTVIEWPROJ;
    ubo.lightDir = glm::vec4(glm::normalize(M_LIGHTDIR), 0.0f);
    ubo.shadowParams = glm::vec4(1.0f / static_cast<float>(SHADOW_MAP_SIZE), M_SHADOWNORMALBIAS, 1.0f, 0.0f);
    ubo.groundParams = M_GROUNDPARAMS;
    ubo.groundExtent = M_GROUNDEXTENT;

    ubo.cameraPos = glm::vec4(m_camera.position, 1.0f);
    ubo.debugParams = glm::vec4(static_cast<float>(m_debugView),
                                static_cast<float>(M_SCENECOPYMIPS - 1),
                                static_cast<float>(M_SWAPCHAINEXTENT.width),
                                static_cast<float>(M_SWAPCHAINEXTENT.height));
    ubo.iblParams = glm::vec4(M_IBLMAXMIP, 1.0f, M_IBLFROMFILE ? 0.0f : 1.0f, 0.0f);

    std::memcpy(M_UNIFORMBUFFERSMAPPED[currentImage], &ubo, sizeof(ubo));
}



#include "../AlethiaV3.h"
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <chrono>

#include <glm/gtc/matrix_transform.hpp>

void AlethiaV3::createDescriptorSetLayout()
{
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::array<VkDescriptorSetLayoutBinding, 2> bindings = {
        uboLayoutBinding, samplerLayoutBinding
    };

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

void AlethiaV3::onMouseMove(double xpos, double ypos)
{
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

    m_deltaTime = time - m_lastFrameTime;
    m_lastFrameTime = time;

    m_camera.processKeyboard(m_window, m_deltaTime);

    UniformBufferObject ubo{};

    ubo.model = glm::mat4(1.0f);
    ubo.view = m_camera.view();

    ubo.proj = glm::perspective(glm::radians(45.0f),
                                M_SWAPCHAINEXTENT.width / static_cast<float>(M_SWAPCHAINEXTENT.height),
                                0.1f, 1000.0f);

    ubo.proj[1][1] *= -1;

    std::memcpy(M_UNIFORMBUFFERSMAPPED[currentImage], &ubo, sizeof(ubo));
}



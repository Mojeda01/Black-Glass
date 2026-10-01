#include "../AlethiaV3.h"
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <chrono>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/gtc/matrix_transform.hpp>

void AlethiaV3::createDescriptorSetLayout()
{
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboLayoutBinding;

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

void AlethiaV3::updateUniformBuffer(uint32_t currentImage)
{
    static auto startTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float>(currentTime - startTime).count();

    UniformBufferObject ubo{};

    ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(45.0f),
                            glm::vec3(0.0f, 0.0f, 1.0f));

    ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f),
                            glm::vec3(0.0f, 0.0f, 0.0f),
                            glm::vec3(0.0f, 0.0f, 1.0f));

    ubo.proj = glm::perspective(glm::radians(45.0f),
                                M_SWAPCHAINEXTENT.width / static_cast<float>(M_SWAPCHAINEXTENT.height),
                                0.1f, 10.0f);

    ubo.proj[1][1] *= -1;

    std::memcpy(M_UNIFORMBUFFERSMAPPED[currentImage], &ubo, sizeof(ubo));
}



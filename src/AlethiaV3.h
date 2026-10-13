#pragma once 
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include "Camera.h"

#include <vector>
#include <array>
#include <string>
#include <optional>


// forward struct for query helper.
struct QueueFamilyIndices{
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;
    bool isComplete() const {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

struct SwapchainSupportDetails{
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

struct AllocatedBuffer 
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
};

struct AllocatedImage
{
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
};

struct Vertex 
{
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord;
    glm::vec3 normal;
    glm::vec4 tangent;

    static VkVertexInputBindingDescription getBindingDescription()
    {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 5> getAttributeDescriptions() 
    {
        std::array<VkVertexInputAttributeDescription, 5> attributeDescriptions{}; 
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, pos);
        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex, color);

        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

        attributeDescriptions[3].binding = 0;
        attributeDescriptions[3].location = 3;
        attributeDescriptions[3].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[3].offset = offsetof(Vertex, normal);

        attributeDescriptions[4].binding = 0;
        attributeDescriptions[4].location = 4;
        attributeDescriptions[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[4].offset = offsetof(Vertex, tangent);

        return attributeDescriptions;
    }
};

constexpr uint32_t MATERIAL_TEXTURE_SLOTS = 5;

struct Primitive
{
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    int materialIndex = 0;
    glm::mat4 worldMatrix = glm::mat4(1.0f);
};

struct ImageData
{
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
};

struct SamplerData
{
    VkFilter magFilter = VK_FILTER_LINEAR;
    VkFilter minFilter = VK_FILTER_LINEAR;
    VkSamplerAddressMode wrapS = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode wrapT = VK_SAMPLER_ADDRESS_MODE_REPEAT;
};

struct TextureData
{
    int image = -1;
    int sampler = -1;
    bool srgb = true;
};

struct MaterialParams
{
    glm::vec4 baseColorFactor{ 1.0f };
    glm::vec4 emissiveFactor{ 0.0f, 0.0f, 0.0f, 0.5f };
    glm::vec4 factors{ 0.0f, 0.5f, 1.0f, 1.0f };
    glm::ivec4 flags0{ 0 };
    glm::ivec4 flags1{ 0 };
    glm::vec4 uvLinear[MATERIAL_TEXTURE_SLOTS] = {
        { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f },
        { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }
    };
    glm::vec4 uvOffset[MATERIAL_TEXTURE_SLOTS] = {};
};

struct Material
{
    MaterialParams params;
    std::array<int, MATERIAL_TEXTURE_SLOTS> textures{ -1, -1, -1, -1, -1 };
};

struct GpuTexture
{
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<Primitive> primitives;
    std::vector<ImageData> images;
    std::vector<SamplerData> samplers;
    std::vector<TextureData> textures;
    std::vector<Material> materials;
};

struct UniformBufferObject 
{
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec4 cameraPos;
    glm::vec4 debugParams;
};

// Number of frame the CPU is allowed to record ahead of the GPU.
constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

// validation layer code.
const std::vector<const char*> VALIDATION_LAYERS = 
{
    "VK_LAYER_KHRONOS_validation"
};

// Device extensions
const std::vector<const char*> DEVICE_EXTENSIONS = 
{
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
    VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
    VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
    VK_KHR_MULTIVIEW_EXTENSION_NAME,
    VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
    VK_EXT_MEMORY_BUDGET_EXTENSION_NAME,
#ifdef __APPLE__
    "VK_KHR_portability_subset",
#endif
    "VK_KHR_maintenance4",
    "VK_KHR_maintenance5",
    "VK_KHR_maintenance6" 
};

// populate the debug messenger.
void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);

class AlethiaV3{
public:
    explicit AlethiaV3(GLFWwindow* window);
    ~AlethiaV3();

    void drawFrame();
    void initVulkan();
    bool isInitialized() const { return M_INITIALIZED; }

    void setFramebufferResized() { m_framebufferResized = true; }

    void onMouseMove(double xpos, double ypos);
    void setMouseCaptured(bool captured);
    void setDebugView(int view) { m_debugView = view; }
    bool mouseCaptured() const { return m_mouseCaptured; }
    Camera& camera() { return m_camera; }
private:
    GLFWwindow* m_window = nullptr;
    bool m_framebufferResized = false;

    Camera m_camera{};
    bool m_firstMouse = true;
    bool m_mouseCaptured = false;
    int m_debugView = 0;
    double m_lastX = 0.0;
    double m_lastY = 0.0;
    float m_deltaTime = 0.0f;
    float m_lastFrameTime = 0.0f;

    // CORE
    VkInstance M_INSTANCE = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT M_DEBUGMESSENGER = VK_NULL_HANDLE; 
    VkPhysicalDevice M_PHYSICALDEVICE = VK_NULL_HANDLE;
    VkDevice M_DEVICE = VK_NULL_HANDLE;
    VkQueue M_GRAPHICSQUEUE = VK_NULL_HANDLE;
    VkQueue M_PRESENTQUEUE = VK_NULL_HANDLE;
    VkSurfaceKHR M_SURFACE = VK_NULL_HANDLE;

    // SWAPCHAIN
    VkSwapchainKHR M_SWAPCHAIN = VK_NULL_HANDLE;
    std::vector<VkImage> M_SWAPCHAINIMAGES;
    std::vector<VkImageView> M_SWAPCHAINIMAGEVIEWS;
    VkFormat M_SWAPCHAINIMAGEFORMAT{};
    VkExtent2D M_SWAPCHAINEXTENT{};

    // DEPTH
    AllocatedImage M_DEPTHIMAGE{};
    
    // TEXTURE
    std::vector<AllocatedImage> M_TEXTUREIMAGES;
    AllocatedImage M_FALLBACKTEXTURE{};
    std::vector<VkSampler> M_SAMPLERS;
    std::vector<GpuTexture> M_TEXTURES;
    std::vector<Material> M_MATERIALS;
    AllocatedBuffer M_MATERIALBUFFER{};
    VkDeviceSize M_MATERIALSTRIDE = 0;
    VkSampler M_TEXTURESAMPLER = VK_NULL_HANDLE;

    // PIPELINE 
    VkPipelineLayout M_PIPELINELAYOUT = VK_NULL_HANDLE;
    VkPipeline M_GRAPHICSPIPELINE = VK_NULL_HANDLE;

    // COMMANDS 
    VkCommandPool M_COMMANDPOOL = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> M_COMMANDBUFFERS;

    // GEOMETRY
    AllocatedBuffer M_VERTEXBUFFER{};
    AllocatedBuffer M_INDEXBUFFER{};
    uint32_t M_INDEXCOUNT = 0;
    std::vector<Primitive> M_PRIMITIVES;
    
    // UNIFORMS
    std::vector<AllocatedBuffer> M_UNIFORMBUFFERS;
    std::vector<void*> M_UNIFORMBUFFERSMAPPED;
    VkDescriptorSetLayout M_DESCRIPTORSETLAYOUT = VK_NULL_HANDLE;
    PFN_vkCmdPushDescriptorSetKHR M_PUSHDESCRIPTORFN = nullptr;

    // SYNC
    std::vector<VkSemaphore> M_IMAGEAVAILABLESEMAPHORES;
    std::vector<VkSemaphore> M_RENDERFINISHEDSEMAPHORES;
    std::vector<VkFence> M_INFLIGHTFENCES;
    uint32_t M_CURRENTFRAME = 0;
    bool M_INITIALIZED = false;
    
    // SETUP
    void createInstance();          // done
    void setupDebugMessenger();     // done 
    void createSurface();           // done
    void pickPhysicalDevice();      // done
    void createLogicalDevice();     // done

    // SWAPCHAIN
    void createSwapchain();     // done
    void createImageViews();    // done 
    void recreateSwapchain();
    void cleanupSwapchain();
    void createDepthResources();

    // PIPELINE 
    void createPipelineLayout();
    void createGraphicsPipeline();
    void createVertexBuffer();
    void setGeometry(const std::vector<Vertex>& verts,
                        const std::vector<uint32_t>& idx);
    void setPrimitives(const std::vector<Primitive>& prims);
    void createIndexBuffer();
    void createDescriptorSetLayout();
    void createUniformBuffers();
    void updateUniformBuffer(uint32_t currentImage);
    AllocatedImage createTextureFromPixels(const unsigned char* pixels, int width, int height,
                                            VkFormat format);
    void createTextures(const MeshData& mesh);
    void createMaterialBuffer();
    VkSampler createSamplerObject(const SamplerData& data);
    MeshData loadGltf(const std::string& path);
    void createTextureSampler();
    void transitionImageLayoutImmediate(VkImage image, VkImageLayout oldLayout,
                                        VkImageLayout newLayout);
    void copyBufferToImage(VkBuffer buffer, VkImage image,
                            uint32_t width, uint32_t height);

    // COMMANDS AND SYNC 
    void createCommandPool();   // done
    void createCommandBuffers(); // done
    void createSyncObjects(); // done 
    void recordCommandBuffers(VkCommandBuffer commandBuffer, uint32_t imageIndex);

    // QUERY HELPERS
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
    SwapchainSupportDetails querySwapchainSupport(VkPhysicalDevice device);

    // MEMORY
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    VkFormat findDepthFormat();
    AllocatedImage createImage( uint32_t width, uint32_t height, VkFormat format,
                                VkImageTiling tiling, VkImageUsageFlags usage,
                                VkMemoryPropertyFlags properties,
                                VkImageAspectFlags aspectFlags,
                                uint32_t mipLevels = 1);
    void destroyImage(AllocatedImage& image);
    AllocatedBuffer createBuffer(   VkDeviceSize size,
                                    VkBufferUsageFlags usage,
                                    VkMemoryPropertyFlags properties);
    void destroyBuffer(AllocatedBuffer& buffer);
    void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size);
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);

    // SHADER
    VkShaderModule createShaderModule(const std::vector<char>& code);
    static std::vector<char> readFile(const std::string& filename);

    void cleanup();

};

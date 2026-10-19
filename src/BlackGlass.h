#pragma once 
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include "vma/vk_mem_alloc.h"

#include "Camera.h"
#include "MeshData.h"
#include "Editor/Brush.h"
#include "Editor/Light.h"
#include "Debug/DebugDraw.h"
#include "Debug/FrameLog.h"
#include "RenderSettings.h"

#include <vector>
#include <array>
#include <string>
#include <optional>
#include <memory>


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
    VmaAllocation allocation = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDeviceSize size = 0;
};

struct AllocatedImage
{
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
};

struct ModelAsset
{
    std::string name;
    std::string path;
    AllocatedBuffer vertexBuffer{};
    AllocatedBuffer indexBuffer{};
    AllocatedBuffer skinBuffer{};
    std::shared_ptr<Skeleton> skeleton;
    std::vector<AllocatedImage> textureImages;
    std::vector<VkSampler> samplers;
    std::vector<GpuTexture> textures;
    std::vector<Material> materials;
    AllocatedBuffer materialBuffer{};
    VkDeviceSize materialStride = 0;
    std::vector<Primitive> primitives;
    glm::vec3 localMin{ 0.0f };
    glm::vec3 localMax{ 0.0f };
    std::shared_ptr<BrushDef> brush;
    bool marker = false;
};

struct Model
{
    std::shared_ptr<ModelAsset> asset;
    std::string name;
    bool visible = true;
    bool hidden = false;
    bool locked = false;
    bool isGround = false;
    bool ghost = false;
    bool viewModel = false;
    glm::vec3 position{ 0.0f };
    float yawDegrees = 0.0f;
    float scale = 1.0f;
    glm::mat4 transform{ 1.0f };
    std::optional<BrushDef> brush;
    std::optional<LightDef> light;
    bool spawn = false;
    bool solid = true;
    int group = 0;
    glm::vec3 worldMin{ 0.0f };
    glm::vec3 worldMax{ 0.0f };
};

struct CollisionPiece
{
    glm::vec2 center{ 0.0f };
    float cosYaw = 1.0f;
    float sinYaw = 0.0f;
    glm::vec2 minLocal{ 0.0f };
    glm::vec2 maxLocal{ 0.0f };
    float baseY = 0.0f;
    float topOffset = 0.0f;
    float slope = 0.0f;
    bool steep = false;
    float reach = 0.0f;
};

constexpr uint32_t MAX_POINT_LIGHTS = 16;
constexpr uint32_t MAX_JOINTS = 128;

struct UniformBufferObject 
{
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 invViewProj;
    glm::mat4 lightViewProj;
    glm::vec4 cameraPos;
    glm::vec4 debugParams;
    glm::vec4 iblParams;
    glm::vec4 lightDir;
    glm::vec4 shadowParams;
    glm::vec4 groundParams;
    glm::vec4 groundExtent;
    glm::vec4 groundBoxes[8];
    glm::vec4 effectParams;
    glm::vec4 passParams;
    glm::vec4 projParams;
    glm::vec4 lightInfo;
    glm::vec4 lightPositions[MAX_POINT_LIGHTS];
    glm::vec4 lightColors[MAX_POINT_LIGHTS];
    glm::vec4 lightDirections[MAX_POINT_LIGHTS];
    glm::mat4 jointMatrices[MAX_JOINTS];
};

constexpr VkFormat HDR_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr uint32_t IBL_ENV_SIZE = 1024;
constexpr uint32_t IBL_IRRADIANCE_SIZE = 32;
constexpr uint32_t IBL_PREFILTER_SIZE = 256;
constexpr uint32_t IBL_BRDF_SIZE = 256;
constexpr uint32_t IBL_TEXTURE_BINDINGS = 4;
constexpr uint32_t BLOOM_MAX_LEVELS = 6;
constexpr uint32_t SHADOW_BINDING_INDEX = 2 + MATERIAL_TEXTURE_SLOTS + IBL_TEXTURE_BINDINGS;
constexpr uint32_t SCENE_COLOR_BINDING_INDEX = SHADOW_BINDING_INDEX + 1;
constexpr uint32_t AO_BINDING_INDEX = SCENE_COLOR_BINDING_INDEX + 1;
constexpr uint32_t REFLECTION_BINDING_INDEX = AO_BINDING_INDEX + 1;
constexpr uint32_t GRAPHICS_BINDING_COUNT = REFLECTION_BINDING_INDEX + 1;
constexpr uint32_t GROUND_BOX_MAX = 8;
constexpr uint32_t SHADOW_MAP_SIZE = 4096;
constexpr VkFormat SHADOW_FORMAT = VK_FORMAT_D32_SFLOAT;

// Number of frame the CPU is allowed to record ahead of the GPU.
constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

// validation layer code.
#ifdef BLACK_GLASS_VALIDATION
const std::vector<const char*> VALIDATION_LAYERS = 
{
    "VK_LAYER_KHRONOS_validation"
};
#else
const std::vector<const char*> VALIDATION_LAYERS = {};
#endif

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

class Editor;
class EditorHost;
class BlackGlassEditorHost;

class BlackGlass{
    friend class BlackGlassEditorHost;
public:
    explicit BlackGlass(GLFWwindow* window);
    ~BlackGlass();

    void drawFrame();
    void initVulkan();
    bool isInitialized() const { return M_INITIALIZED; }

    void setFramebufferResized() { m_framebufferResized = true; }

    void onMouseMove(double xpos, double ypos);
    void setMouseCaptured(bool captured);
    void setDebugView(int view) { m_debugView = view; }
    bool mouseCaptured() const { return m_mouseCaptured; }
    void adjustExposure(float stops);
    void toggleBloom();
    void initImGui();
    bool uiWantsMouse() const;
    bool uiWantsKeyboard() const;
    void toggleMouseCapture();
    void toggleUi();
    void markFrameLog() { FrameLog::event("mark", glfwGetTime(), 0.0, frameLogView()); }
    void requestQuit();
    bool quitPromptOpen() const { return m_quitPrompt; }
    void cancelQuitPrompt() { m_quitPrompt = false; }
    void onLeftPress(bool shiftHeld, bool altHeld);
    void onLeftRelease();
    void onRightPress();
    void onRightRelease();
    void onScroll(double delta);
    void deleteSelected();
    void drawMainMenuBar();
    void drawLibraryWindow();
    void drawSettingsWindow();
    bool handleEditorKey(int key, int mods, bool repeat);
    void noteEditorInput();
    void toggleWalkMode(bool fromCamera = false);
    void startSplash();
    void skipSplash();
    bool splashActive() const { return m_splashActive; }
    void exitWalkMode();
    bool walkMode() const { return m_walkMode; }
    Camera& camera() { return m_camera; }
    int orthoMode() const { return m_orthoMode; }
private:
    GLFWwindow* m_window = nullptr;
    bool m_framebufferResized = false;
    double m_paceNext = 0.0;
    double m_paceInterval = 0.0;
    double m_paceChecked = -1.0;

    Camera m_camera{};
    bool m_firstMouse = true;
    bool m_mouseCaptured = false;
    int m_debugView = 0;
    float m_fov = 45.0f;
    static constexpr float ORTHO_FOV = 5.0f;
    int m_orthoMode = 0;
    glm::vec3 m_orthoTarget{ 0.0f };
    float m_orthoSize = 10.0f;
    glm::vec3 m_orthoSavedPosition{ 0.0f };
    float m_orthoSavedYaw = -90.0f;
    float m_orthoSavedPitch = 0.0f;
    float activeFov() const;
    float orthoCameraDistance() const;
    void applyOrthoCamera();
    void setOrthoView(int mode);
    void panOrtho(float deltaX, float deltaY);
    void zoomOrtho(float notches);
    void fitOrtho(const glm::vec3& center, float radius);
    void keyboardOrtho(float deltaTime);
    RenderSettings m_settings;
    bool m_shadowDirty = false;
    bool m_imguiReady = false;
    bool m_showUi = true;
    bool m_showLibrary = false;
    bool m_showSettings = false;
    static constexpr uint32_t GPU_TIMESTAMPS = 7;
    static constexpr uint32_t GPU_SEGMENTS = GPU_TIMESTAMPS - 1;
    static constexpr size_t FRAME_HISTORY = 120;
    VkQueryPool M_TIMESTAMPPOOL = VK_NULL_HANDLE;
    float m_timestampPeriod = 1.0f;
    uint64_t m_timestampMask = 0;
    bool m_timestampsSupported = false;
    bool m_gpuTimingsEnabled = false;
    bool m_timestampRecorded[MAX_FRAMES_IN_FLIGHT] = {};
    float m_gpuSegmentsMs[GPU_SEGMENTS] = {};
    std::array<float, FRAME_HISTORY> m_frameTimes{};
    size_t m_frameTimeOffset = 0;
    char m_assetSearch[64] = {};
    float m_fitSize = 10.0f;
    std::string m_settingsStatus;
    std::unique_ptr<EditorHost> m_editorHost;
    std::unique_ptr<Editor> m_editor;
    bool m_brushPurgePending = false;
    bool m_showLightMarkers = true;
    bool m_shadowRefreshPending = false;
    double m_shadowRefreshTime = 0.0;
    static constexpr uint32_t DEBUG_LINE_CAPACITY = 65536;
    DebugDraw m_debugDraw;
    AllocatedBuffer M_DEBUGLINEBUFFERS[MAX_FRAMES_IN_FLIGHT] = {};
    VkPipeline M_DEBUGLINEPIPELINE = VK_NULL_HANDLE;
    VkPipeline M_DEBUGOVERLAYPIPELINE = VK_NULL_HANDLE;
    RenderSettings m_defaultSettings;
    bool m_quitPrompt = false;
    VkFormat m_uiColorFormat = VK_FORMAT_UNDEFINED;
    double m_lastX = 0.0;
    double m_lastY = 0.0;
    float m_deltaTime = 0.0f;
    double m_lastFrameTime = 0.0;

    // CORE
    VkInstance M_INSTANCE = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT M_DEBUGMESSENGER = VK_NULL_HANDLE; 
    VkPhysicalDevice M_PHYSICALDEVICE = VK_NULL_HANDLE;
    VkDevice M_DEVICE = VK_NULL_HANDLE;
    VkQueue M_GRAPHICSQUEUE = VK_NULL_HANDLE;
    VkQueue M_PRESENTQUEUE = VK_NULL_HANDLE;
    VmaAllocator M_ALLOCATOR = VK_NULL_HANDLE;
    VkSurfaceKHR M_SURFACE = VK_NULL_HANDLE;

    // SWAPCHAIN
    VkSwapchainKHR M_SWAPCHAIN = VK_NULL_HANDLE;
    std::vector<VkImage> M_SWAPCHAINIMAGES;
    std::vector<VkImageView> M_SWAPCHAINIMAGEVIEWS;
    VkFormat M_SWAPCHAINIMAGEFORMAT{};
    VkExtent2D M_SWAPCHAINEXTENT{};
    VkPresentModeKHR M_PRESENTMODE = VK_PRESENT_MODE_FIFO_KHR;
    FrameLogView frameLogView() const;

    // DEPTH
    AllocatedImage M_DEPTHIMAGE{};

    // HDR
    VkSampleCountFlagBits M_MSAASAMPLES = VK_SAMPLE_COUNT_1_BIT;
    AllocatedImage M_MSAACOLOR{};
    AllocatedImage M_HDRCOLOR{};
    std::vector<AllocatedImage> M_BLOOMIMAGES;
    AllocatedImage M_SCENECOPY{};
    uint32_t M_SCENECOPYMIPS = 1;
    VkSampler M_SCENESAMPLER = VK_NULL_HANDLE;
    VkSampler M_POSTSAMPLER = VK_NULL_HANDLE;
    VkDescriptorSetLayout M_POSTDESCRIPTORLAYOUT = VK_NULL_HANDLE;
    VkPipelineLayout M_POSTPIPELINELAYOUT = VK_NULL_HANDLE;
    VkPipeline M_BLOOMDOWNPIPELINE = VK_NULL_HANDLE;
    VkPipeline M_BLOOMUPPIPELINE = VK_NULL_HANDLE;
    VkPipeline M_COMPOSITEPIPELINE = VK_NULL_HANDLE;

    // EFFECTS
    AllocatedImage M_PREPASSDEPTH{};
    AllocatedImage M_AORAW{};
    AllocatedImage M_AOBLUR{};
    AllocatedImage M_REFLMSAA{};
    AllocatedImage M_REFLCOLOR{};
    AllocatedImage M_REFLDEPTH{};
    VkSampler M_POINTSAMPLER = VK_NULL_HANDLE;
    VkDescriptorSetLayout M_FXDESCRIPTORLAYOUT = VK_NULL_HANDLE;
    VkPipelineLayout M_FXPIPELINELAYOUT = VK_NULL_HANDLE;
    VkPipeline M_PREPASSPIPELINE = VK_NULL_HANDLE;
    VkPipeline M_SSAOPIPELINE = VK_NULL_HANDLE;
    VkPipeline M_SSAOBLURPIPELINE = VK_NULL_HANDLE;
    bool m_ssaoActive = false;
    bool m_reflectionActive = false;
    float m_ssaoWorldRadius = 0.1f;

    // SHADOWS
    AllocatedImage M_SHADOWMAP{};
    VkSampler M_SHADOWSAMPLER = VK_NULL_HANDLE;
    VkPipelineLayout M_SHADOWPIPELINELAYOUT = VK_NULL_HANDLE;
    VkPipeline M_SHADOWPIPELINE = VK_NULL_HANDLE;
    glm::mat4 M_LIGHTVIEWPROJ{ 1.0f };
    glm::mat4 m_viewProj{ 1.0f };
    glm::mat4 m_reflViewProj{ 1.0f };
    glm::vec3 M_LIGHTDIR{ 0.5f, 1.0f, 0.3f };
    glm::vec3 M_SCENEMIN{ 0.0f };
    glm::vec3 M_SCENEMAX{ 0.0f };
    glm::vec4 M_GROUNDPARAMS{ 0.0f };
    glm::vec4 M_GROUNDEXTENT{ 0.0f };
    std::array<glm::vec4, GROUND_BOX_MAX> M_GROUNDBOXES{};
    float M_SHADOWNORMALBIAS = 0.0f;

    // IBL
    AllocatedImage M_IBLENV{};
    AllocatedImage M_IBLIRRADIANCE{};
    AllocatedImage M_IBLPREFILTER{};
    AllocatedImage M_IBLBRDF{};
    VkSampler M_IBLSAMPLER = VK_NULL_HANDLE;
    float M_IBLMAXMIP = 0.0f;
    bool M_IBLFROMFILE = false;
    
    // TEXTURE
    AllocatedImage M_FALLBACKTEXTURE{};
    VkSampler M_TEXTURESAMPLER = VK_NULL_HANDLE;

    // PIPELINE 
    VkPipelineLayout M_PIPELINELAYOUT = VK_NULL_HANDLE;
    VkPipeline M_GRAPHICSPIPELINE = VK_NULL_HANDLE;
    VkPipeline M_SKYPIPELINE = VK_NULL_HANDLE;
    VkPipeline M_TRANSMISSIONPIPELINE = VK_NULL_HANDLE;
    VkPipeline M_BLENDPIPELINE = VK_NULL_HANDLE;
    VkPipeline M_VIEWMODELPIPELINE = VK_NULL_HANDLE;

    // COMMANDS 
    VkCommandPool M_COMMANDPOOL = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> M_COMMANDBUFFERS;

    // GEOMETRY
    std::vector<std::shared_ptr<ModelAsset>> M_LIBRARY;
    std::vector<std::unique_ptr<Model>> M_MODELS;
    std::unique_ptr<Model> M_GROUND;
    Model* m_selected = nullptr;
    int m_groupCounter = 0;
    std::vector<Model*> m_selection;
    Model* m_ghost = nullptr;
    int m_placingAsset = -1;
    bool m_dragging = false;
    glm::vec2 m_dragOffset{ 0.0f };
    bool m_dragVertical = false;
    float m_dragHeightOffset = 0.0f;
    glm::vec3 m_dragPlaneNormal{ 0.0f, 0.0f, 1.0f };
    float m_verticalSnap = 0.5f;
    bool m_flyHeld = false;
    bool m_splashActive = false;
    bool m_splashSkip = false;
    double m_splashStart = -1.0;
    int m_splashColumns = 1;
    glm::ivec2 m_splashFrameSize{ 1, 1 };
    glm::ivec2 m_splashAtlasSize{ 1, 1 };
    std::vector<int> m_splashEnds;
    AllocatedImage m_splashImage{};
    VkSampler m_splashSampler = VK_NULL_HANDLE;
    VkDescriptorSet m_splashSet = VK_NULL_HANDLE;
    void drawSplash();
    void releaseSplash();
    bool m_walkMode = false;
    bool m_walkGrounded = false;
    bool m_walkUiWasVisible = true;
    glm::vec3 m_walkFeet{ 0.0f };
    glm::vec3 m_walkStart{ 0.0f };
    glm::vec3 m_walkVelocity{ 0.0f };
    glm::vec3 m_walkPrevFeet{ 0.0f };
    float m_walkAccumulator = 0.0f;
    float m_walkEyeY = 0.0f;
    bool m_walkEyeValid = false;
    float m_walkBobPhase = 0.0f;
    float m_walkBobAmount = 0.0f;
    float m_walkLandImpact = 0.0f;
    float m_walkLandOffset = 0.0f;
    float m_walkFovBoost = 0.0f;
    float m_walkRoll = 0.0f;
    float m_walkLastYaw = 0.0f;
    float m_walkSmoothDt = 0.0f;
    float m_walkBobY = 0.0f;
    float m_walkBobYVelocity = 0.0f;
    float m_walkBobX = 0.0f;
    float m_walkBobXVelocity = 0.0f;
    float m_walkLandShown = 0.0f;
    float m_walkLandVelocity = 0.0f;
    glm::vec3 m_savedCameraPosition{ 0.0f };
    float m_savedCameraYaw = -90.0f;
    float m_savedCameraPitch = 0.0f;
    std::vector<CollisionPiece> m_collision;
    void buildCollision();
    void loadViewModel();
    void updateViewModel(float deltaTime, float sprintFraction);
    void destroyViewModel();
    std::unique_ptr<Model> m_viewModel;
    bool m_viewModelTried = false;
    bool m_viewModelReady = false;
    int m_viewIdleClip = -1;
    int m_viewWalkClip = -1;
    int m_viewRunClip = -1;
    float m_viewIdleTime = 0.0f;
    glm::vec3 m_viewEye{ 0.0f };
    float m_viewUnits = 1.0f;
    glm::vec3 m_viewOffset{ 0.10f, -0.04f, -0.10f };
    float m_viewSize = 0.6f;
    std::string m_viewDebug;
    std::vector<glm::mat4> m_viewJoints;
    void updateWalk(float deltaTime);
    void stepWalk(float dt);
    std::string m_loadError;
    std::vector<std::string> m_assetFiles;
    
    // UNIFORMS
    std::vector<AllocatedBuffer> M_UNIFORMBUFFERS;
    std::vector<void*> M_UNIFORMBUFFERSMAPPED;
    std::vector<AllocatedBuffer> M_REFLUNIFORMBUFFERS;
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
    void updateStateLog();
    void paceFrame();
    void cleanupSwapchain();
    void createDepthResources();
    void pickMsaaSamples();
    void createHdrResources();
    void destroyHdrResources();
    void createPostResources();
    void createEffectPipelines();
    void createEffectResources();
    void destroyEffectResources();
    void createIBL();
    void classifyPrimitives(MeshData& mesh);
    void scanAssets();
    int importAsset(const std::string& path);
    Model* addInstance(int assetIndex, bool autoPlace);
    bool isSelected(const Model* model) const;
    void clearSelection();
    void selectOnly(Model* model);
    void addToSelection(Model* model);
    void toggleSelection(Model* model);
    void expandGroups();
    int groupSelection();
    int ungroupSelection();
    int allocateGroup();
    void noteGroup(int group);
    void rotateSelection(float degrees);
    void removeModel(size_t index);
    void removeAsset(size_t index);
    void destroyAsset(ModelAsset& asset);
    void beginPlacement(int assetIndex);
    void cancelPlacement();
    void updateInteraction();
    bool cursorRay(glm::vec3& origin, glm::vec3& direction) const;
    Model* pickModel(const glm::vec3& origin, const glm::vec3& direction, float& distance);
    void updateModelTransform(Model& model);
    void placeNewModel(Model& model);
    void createGroundModel();
    void refreshScene();
    void frameScene();
    void createShadowMap();
    void bakeShadowMap();
    void beginUiFrame();
    void buildUi();
    void drawWalkDebug();
    void drawQuitPrompt();
    void shutdownImGui();

    // PIPELINE 
    void createPipelineLayout();
    void createGraphicsPipeline();
    void uploadModelGeometry(ModelAsset& model, const MeshData& mesh);
    void createDescriptorSetLayout();
    void createUniformBuffers();
    void updateUniformBuffer(uint32_t currentImage);
    AllocatedImage createTextureFromPixels(const unsigned char* pixels, int width, int height,
                                            VkFormat format);
    void createFallbackTexture();
    void createModelTextures(ModelAsset& model, const MeshData& mesh);
    void createModelMaterialBuffer(ModelAsset& model);
    VkSampler createSamplerObject(const SamplerData& data);
    MeshData loadGltf(const std::string& path);
    int registerAsset(MeshData& mesh, const std::string& name, const std::string& path);
    void replaceAssetMesh(ModelAsset& asset, MeshData& mesh);
    void purgeUnusedBrushAssets();
    void applySunDirection();
    void createDebugDraw();
    void deferShadowUpdate();
    void initEditor();
    void createTextureSampler();
    void transitionImageLayoutImmediate(VkImage image, VkImageLayout oldLayout,
                                        VkImageLayout newLayout);
    void copyBufferToImage(VkBuffer buffer, VkImage image,
                            uint32_t width, uint32_t height);

    // COMMANDS AND SYNC 
    void createCommandPool();   // done
    void createCommandBuffers(); // done
    void createSyncObjects(); // done 
    void createTimestampPool();
    void collectGpuTimings(uint32_t frameSlot);
    void writeTimestamp(VkCommandBuffer commandBuffer, uint32_t index);
    void recordCommandBuffers(VkCommandBuffer commandBuffer, uint32_t imageIndex);

    // QUERY HELPERS
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
    SwapchainSupportDetails querySwapchainSupport(VkPhysicalDevice device);

    // MEMORY
    void createAllocator();
    void destroyAllocator();
    VkFormat findDepthFormat();
    AllocatedImage createImage( uint32_t width, uint32_t height, VkFormat format,
                                VkImageTiling tiling, VkImageUsageFlags usage,
                                VkMemoryPropertyFlags properties,
                                VkImageAspectFlags aspectFlags,
                                uint32_t mipLevels = 1,
                                uint32_t arrayLayers = 1,
                                VkImageCreateFlags flags = 0,
                                VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D,
                                VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);
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

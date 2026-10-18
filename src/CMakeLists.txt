# src/CMakeLists.txt

# --- Shader compilation ---
set(V3_SHADER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(V3_SHADER_BIN_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${V3_SHADER_BIN_DIR})

find_program(SLANGC slangc)
if (NOT SLANGC)
    message(FATAL_ERROR "slangc not found. Install Vulkan SDK (1.3.296.0+).")
endif()

file(GLOB V3_SHADERS ${V3_SHADER_SRC_DIR}/*.slang)
set(V3_SPV_FILES "")


foreach(SHADER ${V3_SHADERS})
    get_filename_component(SHADER_NAME ${SHADER} NAME_WE)
    set(SPV_FILE ${V3_SHADER_BIN_DIR}/${SHADER_NAME}.spv)
    add_custom_command(
        OUTPUT ${SPV_FILE}
        COMMAND ${SLANGC} ${SHADER} -target spirv -profile glsl_450 -o ${SPV_FILE}
        DEPENDS ${SHADER}
        COMMENT "Compiling Slang shader ${SHADER_NAME}"
    )
    list(APPEND V3_SPV_FILES ${SPV_FILE})
endforeach()


add_custom_target(BlackGlassShaders DEPENDS ${V3_SPV_FILES})

add_library(black_glass_engine STATIC
    initBlackGlass.cpp
    BlackGlass.cpp
    Vulkan/createInstance.cpp
    Vulkan/debugMessenger.cpp
    Vulkan/createSurface.cpp
    Vulkan/pickPhysicalDevice.cpp
    Vulkan/createLogicalDevice.cpp
    Vulkan/createSwapchain.cpp
    Vulkan/createImageViews.cpp
    Vulkan/recreateSwapchain.cpp
    Vulkan/memory.cpp
    Vulkan/createCommandPool.cpp 
    Vulkan/createCommandBuffers.cpp
    Vulkan/createSyncObjects.cpp
    Vulkan/createPipelineLayout.cpp
    Vulkan/createGraphicsPipeline.cpp
    Vulkan/recordCommandBuffers.cpp
    Vulkan/createGeometry.cpp
    Vulkan/createDepthResources.cpp
    Vulkan/createDescriptors.cpp
    Vulkan/createTexture.cpp
    Vulkan/createIBL.cpp
    Vulkan/createHdr.cpp
    Vulkan/createEffects.cpp
    Vulkan/scene.cpp
    Vulkan/placement.cpp
    Vulkan/walk.cpp
    Vulkan/viewModel.cpp
    Vulkan/orthoView.cpp
    Vulkan/splash.cpp
    Vulkan/createShadow.cpp
    Vulkan/ui.cpp
    Vulkan/vmaImpl.cpp
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/imgui_impl_glfw.cpp
    imgui/imgui_impl_vulkan.cpp
    Vulkan/loadGltf.cpp
    Vulkan/editorHost.cpp
    Vulkan/createDebugDraw.cpp
    Debug/DebugDraw.cpp
    Editor/BrushTool.cpp
    Editor/BrushHandles.cpp
    Editor/RotationGizmo.cpp
    Editor/TranslateGizmo.cpp
    Editor/BrushMesh.cpp
    Editor/ProceduralTextures.cpp
    Editor/LevelIO.cpp
    Editor/Editor.cpp
    Editor/EditorUi.cpp
    initVulkan.cpp
)

target_include_directories(black_glass_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(black_glass_engine PUBLIC
    Vulkan::Vulkan
    glfw
)

if(APPLE)
    target_link_libraries(black_glass_engine PUBLIC
        "-framework Metal"
        "-framework Foundation"
        "-framework QuartzCore"
        "-framework CoreGraphics"
        "-framework IOSurface"
    )
    target_compile_definitions(black_glass_engine PRIVATE MVK_CONFIG_LOG_LEVEL=1)
endif()

target_compile_definitions(black_glass_engine PRIVATE
    BLACK_GLASS_SHADER_DIR="${V3_SHADER_BIN_DIR}"
    BLACK_GLASS_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
    BLACK_GLASS_LOADING_DIR="${CMAKE_CURRENT_SOURCE_DIR}/loading"
    BLACK_GLASS_LOG_DIR="${CMAKE_SOURCE_DIR}/log"
)

target_compile_definitions(black_glass_engine PUBLIC
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_FORCE_RADIANS
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(black_glass_engine PRIVATE -Wno-nullability-completeness)
    set_source_files_properties(Vulkan/loadGltf.cpp PROPERTIES COMPILE_OPTIONS "-Wno-switch")
endif()

add_dependencies(black_glass_engine BlackGlassShaders)

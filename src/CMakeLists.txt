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


add_custom_target(AlethiaV3Shaders DEPENDS ${V3_SPV_FILES})

add_library(alethia_v3 STATIC
    initv3.cpp
    AlethiaV3.cpp
    Vulkan/createInstance.cpp
    Vulkan/debugMessenger.cpp
    Vulkan/createSurface.cpp
    Vulkan/pickPhysicalDevice.cpp
    Vulkan/createLogicalDevice.cpp
    Vulkan/createSwapchain.cpp
    Vulkan/createImageViews.cpp
    Vulkan/createCommandPool.cpp 
    Vulkan/createCommandBuffers.cpp
    Vulkan/createSyncObjects.cpp
    Vulkan/createPipelineLayout.cpp
    Vulkan/createGraphicsPipeline.cpp
    Vulkan/recordCommandBuffers.cpp
    initVulkan.cpp
)

target_include_directories(alethia_v3 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(alethia_v3 PUBLIC
    Vulkan::Vulkan
    glfw
)

if(APPLE)
    target_link_libraries(alethia_v3 PUBLIC
        "-framework Metal"
        "-framework Foundation"
        "-framework QuartzCore"
        "-framework CoreGraphics"
        "-framework IOSurface"
    )
    target_compile_definitions(alethia_v3 PRIVATE MVK_CONFIG_LOG_LEVEL=1)
endif()

target_compile_definitions(alethia_v3 PRIVATE
    ALETHIA_V3_SHADER_DIR="${V3_SHADER_BIN_DIR}"
)

add_dependencies(alethia_v3 AlethiaV3Shaders)

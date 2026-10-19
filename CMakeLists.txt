# plasma/CMakeLists.txt

cmake_minimum_required(VERSION 3.22)
project(BlackGlass VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(BLACK_GLASS_VALIDATION_DEFAULT ON)
else()
    set(BLACK_GLASS_VALIDATION_DEFAULT OFF)
endif()
option(BLACK_GLASS_VALIDATION "Enable the Vulkan validation layer" ${BLACK_GLASS_VALIDATION_DEFAULT})

option(BLACK_GLASS_COCOA_PROBE "Log Cocoa window and CAMetalLayer state into the state log (macOS only)" ON)
if(APPLE AND BLACK_GLASS_COCOA_PROBE)
    enable_language(OBJCXX)
endif()

find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)

add_subdirectory(src)

add_executable(black_glass src/main.cpp)
target_link_libraries(black_glass PRIVATE black_glass_engine)

# plasma/CMakeLists.txt

cmake_minimum_required(VERSION 3.22)
project(Alethia VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)

add_subdirectory(src)

add_executable(alethia src/main.cpp)
target_link_libraries(alethia PRIVATE alethia_v3)
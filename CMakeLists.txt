cmake_minimum_required(VERSION 3.22)
project(VulkanEngine VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
foreach(_config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_config} ${CMAKE_BINARY_DIR}/bin)
endforeach()

find_package(Vulkan   REQUIRED)
find_package(glfw3    REQUIRED)
find_package(Threads  REQUIRED)

include(cmake/imgui.cmake)
include(cmake/shaders.cmake)

add_subdirectory(engine)
add_subdirectory(example)

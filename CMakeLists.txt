cmake_minimum_required(VERSION 3.22)
project(VulkanEngine VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)


find_package(Vulkan REQUIRED)
find_package(glfw3  REQUIRED)

# =============================================================================
# Check ImGui submodule
# =============================================================================

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/imgui)

if(NOT EXISTS ${IMGUI_DIR}/imgui.h)
    message(FATAL_ERROR
        "\n"
        "  ImGui submodule не найден: ${IMGUI_DIR}/imgui.h отсутствует.\n"
        "  Инициализируй submodule:\n"
        "    git submodule update --init --recursive\n"
    )
endif()

foreach(_file
    imgui.cpp
    imgui_draw.cpp
    imgui_tables.cpp
    imgui_widgets.cpp
    backends/imgui_impl_glfw.cpp
    backends/imgui_impl_vulkan.cpp
)
    if(NOT EXISTS ${IMGUI_DIR}/${_file})
        message(FATAL_ERROR "ImGui: ожидаемый файл не найден: ${IMGUI_DIR}/${_file}")
    endif()
endforeach()

message(STATUS "ImGui найден: ${IMGUI_DIR}")

# =============================================================================
# Статическая библиотека ImGui
# =============================================================================

add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)

target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_link_libraries(imgui PUBLIC
    Vulkan::Vulkan
    glfw
)

# Подавляем предупреждения из чужого кода
target_compile_options(imgui PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-w>
    $<$<CXX_COMPILER_ID:MSVC>:/W0>
)

# =============================================================================
# Компиляция шейдеров
# =============================================================================

find_program(GLSLC glslc HINTS ${Vulkan_INCLUDE_DIR}/../bin REQUIRED)

if(NOT GLSLC)
    message(FATAL_ERROR
        "  glslc не найден. Установи Vulkan SDK или добавь путь к glslc в PATH.\n"
    )
endif()

set(SHADER_SOURCE_DIR ${CMAKE_SOURCE_DIR}/shaders)
set(SHADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/bin/shaders)

file(GLOB_RECURSE SHADER_SOURCES
    ${SHADER_SOURCE_DIR}/*.vert
    ${SHADER_SOURCE_DIR}/*.frag
    ${SHADER_SOURCE_DIR}/*.comp
    ${SHADER_SOURCE_DIR}/*.geom
)

file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})

set(SHADER_SPV_FILES "")

foreach(SHADER_SRC ${SHADER_SOURCES})
    # shaders/foo/bar.vert -> foo/bar.vert.spv
    file(RELATIVE_PATH SHADER_REL ${SHADER_SOURCE_DIR} ${SHADER_SRC})
    set(SHADER_SPV ${SHADER_OUTPUT_DIR}/${SHADER_REL}.spv)

    get_filename_component(SHADER_SPV_DIR ${SHADER_SPV} DIRECTORY)
    file(MAKE_DIRECTORY ${SHADER_SPV_DIR})

    add_custom_command(
        OUTPUT  ${SHADER_SPV}
        COMMAND ${GLSLC} ${SHADER_SRC} -o ${SHADER_SPV}
        DEPENDS ${SHADER_SRC}
        COMMENT "Compile: ${SHADER_REL}"
        VERBATIM
    )

    list(APPEND SHADER_SPV_FILES ${SHADER_SPV})
endforeach()


add_custom_target(shaders ALL DEPENDS ${SHADER_SPV_FILES})

message(STATUS "Find shaders: ${SHADER_SOURCES}")

# =============================================================================
# Основной исполняемый файл
# =============================================================================

add_executable(${PROJECT_NAME}
    main.cpp
)


add_dependencies(${PROJECT_NAME} shaders)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    imgui
    Vulkan::Vulkan
    glfw
    ${CMAKE_DL_LIBS}          # -ldl
    Threads::Threads           # -lpthread
)

find_package(Threads REQUIRED)

# Linux: X11 и связанные библиотеки нужны только там
if(UNIX AND NOT APPLE)
    find_package(X11)
    if(X11_FOUND)
        target_link_libraries(${PROJECT_NAME} PRIVATE
            X11::X11
            X11::Xxf86vm
            X11::Xrandr
            X11::Xi
        )
    endif()
endif()

message(STATUS "-------------------------------------------")
message(STATUS "Project      : ${PROJECT_NAME} v${PROJECT_VERSION}")
message(STATUS "Compiler  : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Vulkan SDK  : ${Vulkan_INCLUDE_DIR}")
message(STATUS "glslc       : ${GLSLC}")
message(STATUS "ImGui       : ${IMGUI_DIR}")
message(STATUS "Bin    : ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "-------------------------------------------")

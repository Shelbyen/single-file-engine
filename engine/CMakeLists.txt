# Header-only библиотека — компилировать нечего, только заголовки
add_library(VulkanEngine INTERFACE)

target_include_directories(VulkanEngine INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(VulkanEngine INTERFACE
    imgui
    Vulkan::Vulkan
    glfw
    Threads::Threads
    ${CMAKE_DL_LIBS}
)

if(UNIX AND NOT APPLE)
    find_package(X11)
    if(X11_FOUND)
        target_link_libraries(VulkanEngine INTERFACE X11::X11)
        foreach(_xlib Xxf86vm Xrandr Xi)
            if(TARGET X11::${_xlib})
                target_link_libraries(VulkanEngine INTERFACE X11::${_xlib})
            endif()
        endforeach()
    endif()
endif()

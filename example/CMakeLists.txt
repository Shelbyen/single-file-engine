add_executable(Example main.cpp)

# Одна строка — тянет за собой всё: Vulkan, glfw, imgui, include paths
target_link_libraries(Example PRIVATE VulkanEngine)

add_dependencies(Example shaders)

if(MSVC)
    target_compile_definitions(Example PRIVATE _CRT_SECURE_NO_WARNINGS)
    set_target_properties(Example PROPERTIES
        WIN32_EXECUTABLE $<CONFIG:Release>
    )
endif()

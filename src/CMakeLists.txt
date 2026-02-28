# -----------------------------------------------------------------
# Parallax executable
# -----------------------------------------------------------------

add_executable(parallax
    main.cpp
    core/logger.cpp
    core/window.cpp
    vulkan/context.cpp
    vulkan/swapchain.cpp
)

target_include_directories(parallax PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# -----------------------------------------------------------------
# Link dependencies
# -----------------------------------------------------------------
target_link_libraries(parallax PRIVATE
    Vulkan::Vulkan
    SDL2::SDL2
    SDL2::SDL2main
    glm::glm
    spdlog::spdlog
    GPUOpen::VulkanMemoryAllocator
)

# -----------------------------------------------------------------
# Ensure shaders are compiled before the executable is built
# -----------------------------------------------------------------
add_dependencies(parallax compile_shaders)

# -----------------------------------------------------------------
# Platform-specific link flags
# -----------------------------------------------------------------
if(PLX_PLATFORM_LINUX)
    target_link_libraries(parallax PRIVATE dl pthread)
endif()
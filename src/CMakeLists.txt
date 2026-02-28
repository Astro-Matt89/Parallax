# -----------------------------------------------------------------
# Parallax executable
# -----------------------------------------------------------------

add_executable(parallax
    main.cpp
    core/logger.cpp
    core/window.cpp
    core/application.cpp
    vulkan/context.cpp
    vulkan/swapchain.cpp
    vulkan/pipeline.cpp
    astro/time_system.cpp
    astro/coordinates.cpp
    catalog/catalog_loader.cpp
    rendering/camera.cpp
)

target_include_directories(parallax PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# -----------------------------------------------------------------
# Compile definitions
# -----------------------------------------------------------------
target_compile_definitions(parallax PRIVATE
    PLX_SHADER_DIR="${SHADER_OUTPUT_DIR}"
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
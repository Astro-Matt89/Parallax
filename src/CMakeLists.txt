# -----------------------------------------------------------------
# Parallax executable
# -----------------------------------------------------------------

add_executable(parallax
    main.cpp
    observation/session_types.hpp
    observation/data_record.hpp
    knowledge/knowledge_level.hpp
    knowledge/measurement_technique.hpp
    knowledge/property_descriptor.hpp
    knowledge/measurement_record.hpp
    knowledge/property_registry.hpp
    knowledge/property_registry.cpp
    knowledge/object_knowledge.hpp
    knowledge/knowledge_database.hpp
    knowledge/knowledge_database.cpp
    knowledge/knowledge_database_init.cpp
    universe/object_id.hpp
    universe/celestial_object.hpp
    universe/data_provider.hpp
    universe/star_catalog_provider.hpp
    universe/star_catalog_provider.cpp
    universe/dso_provider.hpp
    universe/dso_provider.cpp
    universe/solar_system_provider.hpp
    universe/solar_system_provider.cpp
    universe/procedural_provider.hpp
    universe/procedural_provider.cpp
    universe/universe.hpp
    universe/universe.cpp
    universe/healpix/healpix_nested.hpp
    universe/rng/splitmix64.hpp
    universe/rng/xoshiro256ss.hpp
    core/logger.cpp
    core/window.cpp
    core/application.cpp
    core/input.cpp
    vulkan/context.cpp
    vulkan/swapchain.cpp
    vulkan/pipeline.cpp
    astro/time_system.cpp
    astro/coordinates.cpp
    astro/atmosphere.cpp
    astro/sky_state.cpp
    astro/solar_system.cpp
    catalog/catalog_loader.cpp
    catalog/dso_loader.cpp
    catalog/spatial_index.cpp
    catalog/visibility_filter.cpp
    rendering/camera.cpp
    rendering/sky_background.cpp
    rendering/solar_system_renderer.cpp
    rendering/solar_system_renderer_helpers.cpp
    rendering/starfield.cpp
    rendering/line_renderer.cpp
    rendering/dso_renderer.cpp
    overlay/constellations.cpp
    overlay/coord_grid.cpp
    overlay/horizon.cpp
    ui/font.cpp
    ui/hud.cpp
    ui/panel_system.cpp
    ui/widgets.cpp
    ui/toolbar.cpp
    ui/side_panel.cpp
    ui/selection.cpp
    ui/info_panel.cpp
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
    nlohmann_json::nlohmann_json
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
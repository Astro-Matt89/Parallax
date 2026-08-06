# -----------------------------------------------------------------
# Parallax unit tests
# -----------------------------------------------------------------

find_package(doctest CONFIG REQUIRED)

# -----------------------------------------------------------------
# Test: TimeSystem
# -----------------------------------------------------------------
add_executable(test_time_system
test_time_system.cpp
"${CMAKE_SOURCE_DIR}/src/astro/time_system.cpp"
)

target_include_directories(test_time_system PRIVATE
"${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(test_time_system PRIVATE
doctest::doctest
glm::glm
)

add_test(NAME TimeSystem COMMAND test_time_system)

# -----------------------------------------------------------------
# Test: Coordinates
# -----------------------------------------------------------------
add_executable(test_coordinates
test_coordinates.cpp
"${CMAKE_SOURCE_DIR}/src/astro/coordinates.cpp"
"${CMAKE_SOURCE_DIR}/src/astro/time_system.cpp"
)

target_include_directories(test_coordinates PRIVATE
"${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(test_coordinates PRIVATE
doctest::doctest
glm::glm
)

add_test(NAME Coordinates COMMAND test_coordinates)

# -----------------------------------------------------------------
# Test: CatalogLoader
# -----------------------------------------------------------------
add_executable(test_catalog_loader
test_catalog_loader.cpp
"${CMAKE_SOURCE_DIR}/src/catalog/catalog_loader.cpp"
"${CMAKE_SOURCE_DIR}/src/core/logger.cpp"
)

target_include_directories(test_catalog_loader PRIVATE
"${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(test_catalog_loader PRIVATE
doctest::doctest
glm::glm
spdlog::spdlog
)

add_test(NAME CatalogLoader COMMAND test_catalog_loader)

# -----------------------------------------------------------------
# Test: Atmosphere
# -----------------------------------------------------------------
add_executable(test_atmosphere
test_atmosphere.cpp
"${CMAKE_SOURCE_DIR}/src/astro/atmosphere.cpp"
)

target_include_directories(test_atmosphere PRIVATE
"${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(test_atmosphere PRIVATE
doctest::doctest
glm::glm
)

add_test(NAME Atmosphere COMMAND test_atmosphere)

# -----------------------------------------------------------------
# Test: SpatialIndex                                ← SPRINT 05 Task 5.0
# -----------------------------------------------------------------
add_executable(test_spatial_index
test_spatial_index.cpp
"${CMAKE_SOURCE_DIR}/src/catalog/spatial_index.cpp"
"${CMAKE_SOURCE_DIR}/src/core/logger.cpp"
)

target_include_directories(test_spatial_index PRIVATE
"${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(test_spatial_index PRIVATE
    doctest::doctest
    glm::glm
    spdlog::spdlog
    )

    add_test(NAME SpatialIndex COMMAND test_spatial_index)

    # -----------------------------------------------------------------
    # Test: SolarSystem                                 ← SPRINT 06 Task 6.1
    # -----------------------------------------------------------------
    add_executable(test_solar_system
    test_solar_system.cpp
    "${CMAKE_SOURCE_DIR}/src/astro/solar_system.cpp"
    "${CMAKE_SOURCE_DIR}/src/astro/time_system.cpp"
    )

    target_include_directories(test_solar_system PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    )

    target_link_libraries(test_solar_system PRIVATE
    doctest::doctest
    glm::glm
    )

    add_test(NAME SolarSystem COMMAND test_solar_system)

# -----------------------------------------------------------------
# Test: SkyState                                    ← SPRINT 06 Task 6.4
# -----------------------------------------------------------------
add_executable(test_sky_state
test_sky_state.cpp
"${CMAKE_SOURCE_DIR}/src/astro/sky_state.cpp"
"${CMAKE_SOURCE_DIR}/src/astro/solar_system.cpp"
"${CMAKE_SOURCE_DIR}/src/astro/coordinates.cpp"
"${CMAKE_SOURCE_DIR}/src/astro/atmosphere.cpp"
"${CMAKE_SOURCE_DIR}/src/astro/time_system.cpp"
)

target_include_directories(test_sky_state PRIVATE
"${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(test_sky_state PRIVATE
doctest::doctest
glm::glm
)

add_test(NAME SkyState COMMAND test_sky_state)

# -----------------------------------------------------------------
# Test: SolarSystemRenderer helpers             ← SPRINT 06 Task 6.5
# -----------------------------------------------------------------
add_executable(test_solar_system_renderer
test_solar_system_renderer.cpp
"${CMAKE_SOURCE_DIR}/src/rendering/solar_system_renderer_helpers.cpp"
)

target_include_directories(test_solar_system_renderer PRIVATE
"${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(test_solar_system_renderer PRIVATE
doctest::doctest
glm::glm
Vulkan::Vulkan
SDL2::SDL2
spdlog::spdlog
)

add_test(NAME SolarSystemRenderer COMMAND test_solar_system_renderer)

# -----------------------------------------------------------------
# Test: SkyBackgroundUbo layout               ← SPRINT 06 Task 6.6
# -----------------------------------------------------------------
add_executable(test_sky_background_ubo
test_sky_background_ubo.cpp
)

target_include_directories(test_sky_background_ubo PRIVATE
"${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(test_sky_background_ubo PRIVATE
doctest::doctest
glm::glm
Vulkan::Vulkan
)

add_test(NAME SkyBackgroundUbo COMMAND test_sky_background_ubo)

# -----------------------------------------------------------------
# Test: KnowledgeCoreTypes                         <- SPRINT 08 Task 8.1
# -----------------------------------------------------------------
add_executable(test_knowledge_core_types
    test_knowledge_core_types.cpp
)

target_include_directories(test_knowledge_core_types PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(test_knowledge_core_types PRIVATE
    doctest::doctest
    glm::glm
)

add_test(NAME KnowledgeCoreTypes COMMAND test_knowledge_core_types)

# -----------------------------------------------------------------
# Test: PropertyRegistry                          <- SPRINT 08 Task 8.2
# -----------------------------------------------------------------
add_executable(test_property_registry
    test_property_registry.cpp
    "${CMAKE_SOURCE_DIR}/src/knowledge/property_registry.cpp"
)

target_include_directories(test_property_registry PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(test_property_registry PRIVATE
    doctest::doctest
    glm::glm
)

add_test(NAME PropertyRegistry COMMAND test_property_registry)

# -----------------------------------------------------------------
# Test: KnowledgeDatabase                         <- SPRINT 08 Task 8.3
# -----------------------------------------------------------------
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(test_knowledge_database
    test_knowledge_database.cpp
    "${CMAKE_SOURCE_DIR}/src/knowledge/knowledge_database.cpp"
    "${CMAKE_SOURCE_DIR}/src/knowledge/property_registry.cpp"
)

target_include_directories(test_knowledge_database PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(test_knowledge_database PRIVATE
    doctest::doctest
    glm::glm
    nlohmann_json::nlohmann_json
)

add_test(NAME KnowledgeDatabase COMMAND test_knowledge_database)

# -----------------------------------------------------------------
# Test: DataArchive                                <- SPRINT 08 Task 8.6
# -----------------------------------------------------------------
add_executable(test_data_archive
    test_data_archive.cpp
    "${CMAKE_SOURCE_DIR}/src/observation/data_archive.cpp"
)

target_include_directories(test_data_archive PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(test_data_archive PRIVATE
    doctest::doctest
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

add_test(NAME DataArchive COMMAND test_data_archive)

# -----------------------------------------------------------------
# Test: PaneTree                                  <- SPRINT 09 Task 9.2
# -----------------------------------------------------------------
add_executable(test_pane_tree
    test_pane_tree.cpp
    "${CMAKE_SOURCE_DIR}/src/ui/shell/pane.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/shell/pane_tree.cpp"
)

target_include_directories(test_pane_tree PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(test_pane_tree PRIVATE
    doctest::doctest
    glm::glm
)

add_test(NAME PaneTree COMMAND test_pane_tree)

target_link_libraries(test_pane_tree PRIVATE
    doctest::doctest
    glm::glm
    Vulkan::Vulkan
)

# -----------------------------------------------------------------
# Test: SnrCalculator                             <- SPRINT 10a Task 10a.2
# -----------------------------------------------------------------
add_executable(test_snr_calculator
    test_snr_calculator.cpp
    "${CMAKE_SOURCE_DIR}/src/instruments/snr_calculator.cpp"
)

target_include_directories(test_snr_calculator PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(test_snr_calculator PRIVATE
    doctest::doctest
    glm::glm
)

add_test(NAME SnrCalculator COMMAND test_snr_calculator)

# -----------------------------------------------------------------
# Test: Psf / Image buffers                       <- SPRINT 10a Task 10a.3
# -----------------------------------------------------------------
add_executable(test_psf
    test_psf.cpp
    "${CMAKE_SOURCE_DIR}/src/imaging/psf.cpp"
)

target_include_directories(test_psf PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(test_psf PRIVATE
    doctest::doctest
    glm::glm
)

add_test(NAME Psf COMMAND test_psf)

# -----------------------------------------------------------------
# Test: ImageFormation                            <- SPRINT 10a Task 10a.4
# -----------------------------------------------------------------
add_executable(test_image_formation
    test_image_formation.cpp
    "${CMAKE_SOURCE_DIR}/src/imaging/psf.cpp"
    "${CMAKE_SOURCE_DIR}/src/imaging/image_formation.cpp"
    "${CMAKE_SOURCE_DIR}/src/instruments/array_instrument.cpp"
    "${CMAKE_SOURCE_DIR}/src/instruments/snr_calculator.cpp"
)

target_include_directories(test_image_formation PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(test_image_formation PRIVATE
    doctest::doctest
    glm::glm
)

add_test(NAME ImageFormation COMMAND test_image_formation)

# -----------------------------------------------------------------
# Test: ImageExporter                            <- SPRINT 10a Task 10a.5
# -----------------------------------------------------------------
add_executable(test_image_exporter
    test_image_exporter.cpp
    "${CMAKE_SOURCE_DIR}/src/imaging/image_exporter.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/user_data_path.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/logger.cpp"
)

target_include_directories(test_image_exporter PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${STB_INCLUDE_DIRS}"
)

target_link_libraries(test_image_exporter PRIVATE
    doctest::doctest
    glm::glm
    spdlog::spdlog
)

add_test(NAME ImageExporter COMMAND test_image_exporter)

# -----------------------------------------------------------------
# Test: ImageAnalyzer                            <- SPRINT 10a Task 10a.8
# -----------------------------------------------------------------
add_executable(test_image_analyzer
    test_image_analyzer.cpp
    "${CMAKE_SOURCE_DIR}/src/analysis/image_analyzer.cpp"
    "${CMAKE_SOURCE_DIR}/src/imaging/image_formation.cpp"
    "${CMAKE_SOURCE_DIR}/src/imaging/psf.cpp"
    "${CMAKE_SOURCE_DIR}/src/instruments/array_instrument.cpp"
    "${CMAKE_SOURCE_DIR}/src/instruments/snr_calculator.cpp"
    "${CMAKE_SOURCE_DIR}/src/knowledge/property_registry.cpp"
    "${CMAKE_SOURCE_DIR}/src/universe/universe.cpp"
    "${CMAKE_SOURCE_DIR}/src/universe/star_catalog_provider.cpp"
    "${CMAKE_SOURCE_DIR}/src/universe/dso_provider.cpp"
    "${CMAKE_SOURCE_DIR}/src/universe/solar_system_provider.cpp"
    "${CMAKE_SOURCE_DIR}/src/universe/procedural_provider.cpp"
    "${CMAKE_SOURCE_DIR}/src/astro/solar_system.cpp"
    "${CMAKE_SOURCE_DIR}/src/astro/time_system.cpp"
    "${CMAKE_SOURCE_DIR}/src/astro/coordinates.cpp"
    "${CMAKE_SOURCE_DIR}/src/catalog/catalog_loader.cpp"
    "${CMAKE_SOURCE_DIR}/src/catalog/dso_loader.cpp"
    "${CMAKE_SOURCE_DIR}/src/catalog/spatial_index.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/logger.cpp"
)

target_include_directories(test_image_analyzer PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(test_image_analyzer PRIVATE
    doctest::doctest
    glm::glm
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

add_test(NAME ImageAnalyzer COMMAND test_image_analyzer)

# -----------------------------------------------------------------
# Test: Ephemeris                                <- SPRINT 10b Task 10b.1
# -----------------------------------------------------------------
add_executable(test_ephemeris
    test_ephemeris.cpp
    "${CMAKE_SOURCE_DIR}/src/interferometry/ephemeris.cpp"
)

target_include_directories(test_ephemeris PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(test_ephemeris PRIVATE
    doctest::doctest
    glm::glm
)

add_test(NAME Ephemeris COMMAND test_ephemeris)

# -----------------------------------------------------------------
# Test: ArrayConfig                              <- SPRINT 10b Task 10b.2
# -----------------------------------------------------------------
add_executable(test_array_config
    test_array_config.cpp
    "${CMAKE_SOURCE_DIR}/src/interferometry/array_config.cpp"
)

target_include_directories(test_array_config PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(test_array_config PRIVATE
    doctest::doctest
    glm::glm
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

add_test(NAME ArrayConfig COMMAND test_array_config)

# -----------------------------------------------------------------
# Test: Mulberry32                               <- SPRINT 10b Task 10b.2
# -----------------------------------------------------------------
add_executable(test_mulberry32
    test_mulberry32.cpp
)

target_include_directories(test_mulberry32 PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(test_mulberry32 PRIVATE
    doctest::doctest
    glm::glm
)

add_test(NAME Mulberry32 COMMAND test_mulberry32)

# -----------------------------------------------------------------
# Test: UvSampling                               <- SPRINT 10b Task 10b.2
# -----------------------------------------------------------------
add_executable(test_uv_sampling
    test_uv_sampling.cpp
    "${CMAKE_SOURCE_DIR}/src/interferometry/ephemeris.cpp"
    "${CMAKE_SOURCE_DIR}/src/interferometry/array_config.cpp"
    "${CMAKE_SOURCE_DIR}/src/interferometry/kolmogorov.cpp"
    "${CMAKE_SOURCE_DIR}/src/interferometry/uv_sampling.cpp"
)

target_include_directories(test_uv_sampling PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(test_uv_sampling PRIVATE
    doctest::doctest
    glm::glm
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

add_test(NAME UvSampling COMMAND test_uv_sampling)

# -----------------------------------------------------------------
# Test: Fft                                      <- SPRINT 10b Task 10b.3
# -----------------------------------------------------------------
add_executable(test_fft
    test_fft.cpp
    "${CMAKE_SOURCE_DIR}/src/core/fft.cpp"
)

target_include_directories(test_fft PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(test_fft PRIVATE
    doctest::doctest
)

add_test(NAME Fft COMMAND test_fft)

# -----------------------------------------------------------------
# Test: Clean (Högbom CLEAN)                     <- SPRINT 10b Task 10b.4
# -----------------------------------------------------------------
add_executable(test_clean
    test_clean.cpp
    "${CMAKE_SOURCE_DIR}/src/core/fft.cpp"
    "${CMAKE_SOURCE_DIR}/src/interferometry/ephemeris.cpp"
    "${CMAKE_SOURCE_DIR}/src/interferometry/array_config.cpp"
    "${CMAKE_SOURCE_DIR}/src/interferometry/kolmogorov.cpp"
    "${CMAKE_SOURCE_DIR}/src/interferometry/uv_sampling.cpp"
    "${CMAKE_SOURCE_DIR}/src/interferometry/imaging.cpp"
    "${CMAKE_SOURCE_DIR}/src/interferometry/clean.cpp"
)

target_include_directories(test_clean PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(test_clean PRIVATE
    doctest::doctest
    glm::glm
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

add_test(NAME Clean COMMAND test_clean)

# -----------------------------------------------------------------
# Test: ClosurePhases                            <- SPRINT 10b Task 10b.5
# -----------------------------------------------------------------
add_executable(test_closure
    test_closure.cpp
    "${CMAKE_SOURCE_DIR}/src/interferometry/ephemeris.cpp"
    "${CMAKE_SOURCE_DIR}/src/interferometry/array_config.cpp"
    "${CMAKE_SOURCE_DIR}/src/interferometry/kolmogorov.cpp"
    "${CMAKE_SOURCE_DIR}/src/interferometry/uv_sampling.cpp"
    "${CMAKE_SOURCE_DIR}/src/interferometry/closure.cpp"
)

target_include_directories(test_closure PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(test_closure PRIVATE
    doctest::doctest
    glm::glm
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

add_test(NAME ClosurePhases COMMAND test_closure)

# -----------------------------------------------------------------
# Test: TargetFamilies                           <- SPRINT 10b Task 10b.6
# -----------------------------------------------------------------
add_executable(test_target_families
    test_target_families.cpp
    "${CMAKE_SOURCE_DIR}/src/procedural/target_families.cpp"
    "${CMAKE_SOURCE_DIR}/src/procedural/target_primitives.cpp"
    "${CMAKE_SOURCE_DIR}/src/procedural/target_recipes.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/fft.cpp"
    "${CMAKE_SOURCE_DIR}/src/interferometry/ephemeris.cpp"
    "${CMAKE_SOURCE_DIR}/src/interferometry/kolmogorov.cpp"
    "${CMAKE_SOURCE_DIR}/src/interferometry/uv_sampling.cpp"
)

target_include_directories(test_target_families PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(test_target_families PRIVATE
    doctest::doctest
    glm::glm
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

add_test(NAME TargetFamilies COMMAND test_target_families)

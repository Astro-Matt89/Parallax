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
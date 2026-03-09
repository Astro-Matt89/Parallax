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
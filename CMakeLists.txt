cmake_minimum_required(VERSION 3.20)
project(Parallax VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Build type defaults
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# -----------------------------------------------------------------------
# Source files
# -----------------------------------------------------------------------
set(PARALLAX_SOURCES
    src/core/math/vec3.cpp
    src/core/math/coordinates.cpp
    src/universe/star.cpp
    src/universe/star_catalog.cpp
    src/universe/procedural_generator.cpp
    src/observatory/telescope.cpp
    src/observatory/atmosphere.cpp
    src/observatory/observer.cpp
    src/rendering/renderer.cpp
    src/rendering/star_field.cpp
    src/discovery/discovery_engine.cpp
)

# -----------------------------------------------------------------------
# Library target (used by both executable and tests)
# -----------------------------------------------------------------------
add_library(parallax_lib STATIC ${PARALLAX_SOURCES})
target_include_directories(parallax_lib PUBLIC ${CMAKE_SOURCE_DIR}/src)

# -----------------------------------------------------------------------
# Main executable
# -----------------------------------------------------------------------
add_executable(parallax src/main.cpp)
target_link_libraries(parallax PRIVATE parallax_lib)

# -----------------------------------------------------------------------
# Tests
# -----------------------------------------------------------------------
enable_testing()

set(TEST_SOURCES
    tests/test_coordinates.cpp
    tests/test_catalog.cpp
    tests/test_atmosphere.cpp
    tests/test_telescope.cpp
    tests/test_procedural.cpp
    tests/test_discovery.cpp
    tests/test_main.cpp
)

add_executable(parallax_tests ${TEST_SOURCES})
target_include_directories(parallax_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(parallax_tests PRIVATE parallax_lib)

add_test(NAME unit_tests COMMAND parallax_tests)

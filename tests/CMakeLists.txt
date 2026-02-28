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
# src/procedural/CMakeLists.txt
# Procedural target-model generator (Sprint 10b Task 10b.6)

target_sources(parallax PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/target_families.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/target_primitives.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/target_recipes.cpp
)

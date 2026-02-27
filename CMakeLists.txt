cmake_minimum_required(VERSION 3.25)

# -----------------------------------------------------------------
# vcpkg toolchain integration (must be set before project())
# -----------------------------------------------------------------
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "vcpkg toolchain file")
endif()

project(parallax
    VERSION 0.1.0
    LANGUAGES CXX
)

# -----------------------------------------------------------------
# C++20 standard — enforced globally
# -----------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# -----------------------------------------------------------------
# Output directories
# -----------------------------------------------------------------
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# -----------------------------------------------------------------
# Platform detection
# -----------------------------------------------------------------
if(WIN32)
    message(STATUS "Platform: Windows")
    add_compile_definitions(PLX_PLATFORM_WINDOWS)
    if(MSVC)
        # Treat warnings as errors, highest warning level
        add_compile_options(/W4 /WX /permissive-)
    endif()
elseif(UNIX AND NOT APPLE)
    message(STATUS "Platform: Linux")
    add_compile_definitions(PLX_PLATFORM_LINUX)
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
else()
    message(WARNING "Unsupported platform — build may not work correctly")
endif()

# -----------------------------------------------------------------
# Find packages (installed via vcpkg)
# -----------------------------------------------------------------
find_package(Vulkan REQUIRED)
find_package(SDL2 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)

# -----------------------------------------------------------------
# Shader compilation (GLSL → SPIR-V via glslangValidator)
# -----------------------------------------------------------------
find_program(GLSLANG_VALIDATOR glslangValidator HINTS "$ENV{VULKAN_SDK}/bin")
if(NOT GLSLANG_VALIDATOR)
    message(FATAL_ERROR "glslangValidator not found. Install the Vulkan SDK.")
endif()
message(STATUS "glslangValidator: ${GLSLANG_VALIDATOR}")

set(SHADER_SOURCE_DIR "${CMAKE_SOURCE_DIR}/shaders")
set(SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")

file(MAKE_DIRECTORY "${SHADER_OUTPUT_DIR}")

# Gather all shader sources
file(GLOB SHADER_SOURCES
    "${SHADER_SOURCE_DIR}/*.vert"
    "${SHADER_SOURCE_DIR}/*.frag"
    "${SHADER_SOURCE_DIR}/*.comp"
)

set(SPIRV_OUTPUTS "")
foreach(SHADER_SRC IN LISTS SHADER_SOURCES)
    get_filename_component(SHADER_NAME "${SHADER_SRC}" NAME)
    set(SPIRV_OUT "${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv")
    list(APPEND SPIRV_OUTPUTS "${SPIRV_OUT}")

    add_custom_command(
        OUTPUT "${SPIRV_OUT}"
        COMMAND "${GLSLANG_VALIDATOR}" -V "${SHADER_SRC}" -o "${SPIRV_OUT}"
        DEPENDS "${SHADER_SRC}"
        COMMENT "Compiling shader: ${SHADER_NAME} -> ${SHADER_NAME}.spv"
        VERBATIM
    )
endforeach()

add_custom_target(compile_shaders ALL DEPENDS ${SPIRV_OUTPUTS})

# -----------------------------------------------------------------
# Source subdirectory
# -----------------------------------------------------------------
add_subdirectory(src)
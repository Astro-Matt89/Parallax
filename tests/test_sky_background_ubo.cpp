/// @file test_sky_background_ubo.cpp
/// @brief Unit tests for SkyUniformData struct layout and SkyParams defaults.
///
/// Verifies that the C++ struct matches the GLSL std140 UBO layout
/// declared in sky_background.frag.  No Vulkan device is needed —
/// only the header is included.
///
/// SPRINT 06 Task 6.6

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "rendering/sky_background.hpp"

#include <cstddef>

using namespace parallax;
using namespace parallax::rendering;

// =================================================================
// Test 1: Total size is a multiple of 16 (std140 vec4 alignment)
// =================================================================

TEST_CASE("SkyUniformData size is a multiple of 16 bytes (std140)")
{
    CHECK(sizeof(SkyUniformData) % 16 == 0);
    // Expected: 48 bytes = 3 × 16
    CHECK(sizeof(SkyUniformData) == 48u);
}

// =================================================================
// Test 2: Member offsets match the documented std140 layout
// =================================================================

TEST_CASE("SkyUniformData member offsets match std140 layout")
{
    // Row 0 (bytes 0–15)
    CHECK(offsetof(SkyUniformData, camera_alt_rad)  == 0u);
    CHECK(offsetof(SkyUniformData, camera_az_rad)   == 4u);
    CHECK(offsetof(SkyUniformData, fov_rad)         == 8u);
    CHECK(offsetof(SkyUniformData, aspect_ratio)    == 12u);

    // Row 1 (bytes 16–31)
    CHECK(offsetof(SkyUniformData, bortle_scale)    == 16u);
    CHECK(offsetof(SkyUniformData, sun_altitude_deg) == 20u);
    CHECK(offsetof(SkyUniformData, sun_azimuth_deg)  == 24u);
    CHECK(offsetof(SkyUniformData, moon_altitude_deg) == 28u);

    // Row 2 (bytes 32–47)
    CHECK(offsetof(SkyUniformData, moon_azimuth_deg)   == 32u);
    CHECK(offsetof(SkyUniformData, moon_illumination)  == 36u);
    CHECK(offsetof(SkyUniformData, atmosphere_enabled) == 40u);
    CHECK(offsetof(SkyUniformData, _pad0)              == 44u);
}

// =================================================================
// Test 3: SkyParams default values are safe for the initial frame
// =================================================================

TEST_CASE("SkyParams default values represent safe initial state")
{
    const SkyParams p{};

    // Atmosphere enabled by default
    CHECK(p.atmosphere_enabled == true);

    // Sun well below the horizon (deep night default)
    CHECK(p.sun_altitude_deg == doctest::Approx(-90.0f));

    // Moon below the horizon
    CHECK(p.moon_altitude_deg == doctest::Approx(-90.0f));

    // No moon glow by default
    CHECK(p.moon_illumination == doctest::Approx(0.0f));

    // Reasonable default Bortle scale
    CHECK(p.bortle_scale > 0.0f);
    CHECK(p.bortle_scale <= 9.0f);
}

// =================================================================
// Test 4: atmosphere_enabled field is exactly 4 bytes (uint32)
// =================================================================

TEST_CASE("SkyUniformData::atmosphere_enabled is 4 bytes (uint32)")
{
    CHECK(sizeof(SkyUniformData::atmosphere_enabled) == 4u);
}

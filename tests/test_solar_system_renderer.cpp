/// @file test_solar_system_renderer.cpp
/// @brief Unit tests for SolarSystemRenderer pure static helpers.
///
/// Tests planet_color, planet_name, and magnitude_to_radius_ndc without
/// requiring a Vulkan device. The three helpers are compiled from
/// solar_system_renderer_helpers.cpp which has no rendering dependencies.
///
/// SPRINT 06 Task 6.5

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "astro/solar_system.hpp"
#include "rendering/solar_system_renderer.hpp"

#include <cmath>
#include <set>
#include <string>

using namespace parallax;
using namespace parallax::rendering;
using namespace parallax::astro::planet_id;

// =================================================================
// Test 1: planet_name — non-empty, distinct strings for all planets
// =================================================================

TEST_CASE("planet_name returns non-empty, distinct strings for Mercury..Neptune")
{
    constexpr u32 kIds[] = {kMercury, kVenus, kMars, kJupiter, kSaturn, kUranus, kNeptune};
    std::set<std::string> names;

    for (const u32 id : kIds)
    {
        const std::string_view name = SolarSystemRenderer::planet_name(id);
        CHECK(!name.empty());
        names.insert(std::string(name));
    }

    // All names must be distinct
    CHECK(names.size() == 7u);
}

TEST_CASE("planet_name returns correct names by planet_id")
{
    CHECK(SolarSystemRenderer::planet_name(kMercury) == "Mercury");
    CHECK(SolarSystemRenderer::planet_name(kVenus)   == "Venus");
    CHECK(SolarSystemRenderer::planet_name(kMars)    == "Mars");
    CHECK(SolarSystemRenderer::planet_name(kJupiter) == "Jupiter");
    CHECK(SolarSystemRenderer::planet_name(kSaturn)  == "Saturn");
    CHECK(SolarSystemRenderer::planet_name(kUranus)  == "Uranus");
    CHECK(SolarSystemRenderer::planet_name(kNeptune) == "Neptune");
}

TEST_CASE("planet_name handles unknown planet_id gracefully")
{
    // Should not return an empty string or crash
    const std::string_view unknown = SolarSystemRenderer::planet_name(99u);
    CHECK(!unknown.empty());
}

// =================================================================
// Test 2: planet_color — per-planet color properties
// =================================================================

TEST_CASE("planet_color: Mars red channel dominates")
{
    const Vec4f mars = SolarSystemRenderer::planet_color(kMars);
    CHECK(mars.r > mars.g);
    CHECK(mars.r > mars.b);
    CHECK(mars.a == doctest::Approx(1.0f));
}

TEST_CASE("planet_color: Neptune blue channel dominates")
{
    const Vec4f neptune = SolarSystemRenderer::planet_color(kNeptune);
    CHECK(neptune.b > neptune.r);
    CHECK(neptune.b > 0.5f);
    CHECK(neptune.a == doctest::Approx(1.0f));
}

TEST_CASE("planet_color: Mercury is near-gray (r ≈ g ≈ b)")
{
    const Vec4f mercury = SolarSystemRenderer::planet_color(kMercury);
    constexpr f32 kTolerance = 0.02f;
    CHECK(std::abs(mercury.r - mercury.g) < kTolerance);
    CHECK(std::abs(mercury.r - mercury.b) < kTolerance);
    CHECK(std::abs(mercury.g - mercury.b) < kTolerance);
}

TEST_CASE("planet_color: all channels in [0, 1]")
{
    constexpr u32 kIds[] = {kMercury, kVenus, kMars, kJupiter, kSaturn, kUranus, kNeptune};
    for (const u32 id : kIds)
    {
        const Vec4f col = SolarSystemRenderer::planet_color(id);
        CHECK(col.r >= 0.0f); CHECK(col.r <= 1.0f);
        CHECK(col.g >= 0.0f); CHECK(col.g <= 1.0f);
        CHECK(col.b >= 0.0f); CHECK(col.b <= 1.0f);
        CHECK(col.a == doctest::Approx(1.0f));
    }
}

// =================================================================
// Test 3: magnitude_to_radius_ndc — bounds and monotonicity
// =================================================================

// Known constant values from solar_system_renderer.hpp:
//   kMinIconRadiusNdc = 0.004f  (faint end)
//   kMaxIconRadiusNdc = 0.012f  (bright end)
static constexpr f32 kKnownMinRadius = 0.004f;
static constexpr f32 kKnownMaxRadius = 0.012f;

TEST_CASE("magnitude_to_radius_ndc: mag -5 returns kMaxIconRadiusNdc (0.012)")
{
    const f32 r = SolarSystemRenderer::magnitude_to_radius_ndc(-5.0f);
    CHECK(r == doctest::Approx(kKnownMaxRadius));
}

TEST_CASE("magnitude_to_radius_ndc: mag +10 returns kMinIconRadiusNdc (0.004)")
{
    const f32 r = SolarSystemRenderer::magnitude_to_radius_ndc(10.0f);
    CHECK(r == doctest::Approx(kKnownMinRadius));
}

TEST_CASE("magnitude_to_radius_ndc: result is clamped to [kMin, kMax]")
{
    // Very bright — should not exceed max
    const f32 r_bright = SolarSystemRenderer::magnitude_to_radius_ndc(-100.0f);
    CHECK(r_bright == doctest::Approx(kKnownMaxRadius));

    // Very faint — should not go below min
    const f32 r_faint = SolarSystemRenderer::magnitude_to_radius_ndc(100.0f);
    CHECK(r_faint == doctest::Approx(kKnownMinRadius));
}

TEST_CASE("magnitude_to_radius_ndc: monotonic decreasing across typical magnitudes")
{
    constexpr f32 kMags[] = {-5.0f, -2.0f, 0.0f, 2.0f, 5.0f, 10.0f};
    constexpr u32 kN = 6;
    f32 prev = SolarSystemRenderer::magnitude_to_radius_ndc(kMags[0]);

    for (u32 i = 1; i < kN; ++i)
    {
        const f32 cur = SolarSystemRenderer::magnitude_to_radius_ndc(kMags[i]);
        // Each brighter → larger radius, so going faint → radius non-increasing
        CHECK(cur <= prev + 1e-6f);
        prev = cur;
    }
}

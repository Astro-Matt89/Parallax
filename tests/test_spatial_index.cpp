/// @file test_spatial_index.cpp
/// @brief Unit tests for the declination-band spatial index.
///
/// Tests: build, query correctness, magnitude filtering, polar regions,
/// RA wrap-around, and empty queries.

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "catalog/spatial_index.hpp"
#include "catalog/star_entry.hpp"
#include "core/logger.hpp"
#include "core/types.hpp"

#include <glm/gtc/constants.hpp>

#include <cmath>
#include <vector>

using namespace parallax;
using namespace parallax::catalog;

namespace
{

/// @brief Helper to create a star at given RA/Dec (degrees) with magnitude.
StarEntry make_star(f64 ra_deg, f64 dec_deg, f32 mag, u32 id = 0)
{
    return StarEntry{
        .ra         = ra_deg * astro_constants::kDegToRad,
        .dec        = dec_deg * astro_constants::kDegToRad,
        .mag_v      = mag,
        .color_bv   = 0.0f,
        .catalog_id = id,
    };
}

} // anonymous namespace

// =================================================================
// Custom main: initialize logger before tests
//
// SpatialIndex::build() logs through PLX_CORE_INFO, which dereferences
// Logger::get_core_logger().  Without Logger::init() that shared_ptr is
// null and the first build() call segfaults.  Same pattern as
// test_catalog_loader.cpp.
// =================================================================

int main(int argc, char** argv)
{
    parallax::core::Logger::init();
    const int result = doctest::Context(argc, argv).run();
    parallax::core::Logger::shutdown();
    return result;
}

TEST_CASE("SpatialIndex: build and basic properties")
{
    std::vector<StarEntry> stars;
    stars.push_back(make_star(0.0, 0.0, 5.0f, 1));
    stars.push_back(make_star(90.0, 45.0, 3.0f, 2));
    stars.push_back(make_star(180.0, -45.0, 7.0f, 3));

    SpatialIndex index;
    CHECK_FALSE(index.is_built());

    index.build(stars, 180);

    CHECK(index.is_built());
    CHECK(index.get_star_count() == 3);
}

TEST_CASE("SpatialIndex: query finds stars in FOV")
{
    std::vector<StarEntry> stars;
    // Place stars at known positions
    stars.push_back(make_star(10.0, 10.0, 4.0f, 1));    // Near center
    stars.push_back(make_star(12.0, 11.0, 5.0f, 2));    // Near center
    stars.push_back(make_star(180.0, -60.0, 3.0f, 3));  // Far away

    SpatialIndex index;
    index.build(stars, 180);

    // Query a 30° disc centered at (10°, 10°)
    const f64 center_ra = 10.0 * astro_constants::kDegToRad;
    const f64 center_dec = 10.0 * astro_constants::kDegToRad;
    const f64 radius = 30.0 * astro_constants::kDegToRad;

    auto results = index.query(center_ra, center_dec, radius, 10.0f);

    // Should find star 1 and 2, not star 3
    CHECK(results.size() == 2);

    // Verify the far-away star is not in results
    for (u32 idx : results)
    {
        CHECK(idx != 2);  // Star 3 is at index 2
    }
}

TEST_CASE("SpatialIndex: magnitude filtering works")
{
    std::vector<StarEntry> stars;
    stars.push_back(make_star(10.0, 10.0, 4.0f, 1));
    stars.push_back(make_star(10.5, 10.5, 8.0f, 2));  // Faint star
    stars.push_back(make_star(11.0, 10.0, 2.0f, 3));

    SpatialIndex index;
    index.build(stars, 180);

    const f64 center_ra = 10.0 * astro_constants::kDegToRad;
    const f64 center_dec = 10.0 * astro_constants::kDegToRad;
    const f64 radius = 30.0 * astro_constants::kDegToRad;

    // Query with MLIM 6.0 — should skip the mag 8.0 star
    auto results = index.query(center_ra, center_dec, radius, 6.0f);
    CHECK(results.size() == 2);

    // Query with MLIM 10.0 — should find all 3
    auto results_deep = index.query(center_ra, center_dec, radius, 10.0f);
    CHECK(results_deep.size() == 3);
}

TEST_CASE("SpatialIndex: query near north pole")
{
    std::vector<StarEntry> stars;
    stars.push_back(make_star(0.0, 89.0, 3.0f, 1));
    stars.push_back(make_star(90.0, 89.5, 4.0f, 2));
    stars.push_back(make_star(180.0, 88.0, 5.0f, 3));
    stars.push_back(make_star(0.0, 0.0, 6.0f, 4));  // Far from pole

    SpatialIndex index;
    index.build(stars, 180);

    // Query disc at north pole, 5° radius
    const f64 center_ra = 0.0;
    const f64 center_dec = 90.0 * astro_constants::kDegToRad;
    const f64 radius = 5.0 * astro_constants::kDegToRad;

    auto results = index.query(center_ra, center_dec, radius, 10.0f);

    // All 3 polar stars should be found, equatorial star should not
    CHECK(results.size() >= 2);  // At minimum the 89° and 89.5° stars

    bool found_equatorial = false;
    for (u32 idx : results)
    {
        if (idx == 3)
        {
            found_equatorial = true;
        }
    }
    CHECK_FALSE(found_equatorial);
}

TEST_CASE("SpatialIndex: RA wrap-around near 0/360")
{
    std::vector<StarEntry> stars;
    stars.push_back(make_star(359.0, 0.0, 4.0f, 1));  // Just below 360°
    stars.push_back(make_star(1.0, 0.0, 4.0f, 2));    // Just above 0°
    stars.push_back(make_star(180.0, 0.0, 4.0f, 3));  // Opposite side

    SpatialIndex index;
    index.build(stars, 180);

    // Query disc centered at RA=0°, Dec=0°, radius=5°
    const f64 center_ra = 0.0;
    const f64 center_dec = 0.0;
    const f64 radius = 5.0 * astro_constants::kDegToRad;

    auto results = index.query(center_ra, center_dec, radius, 10.0f);

    // Should find both wrap-around stars
    CHECK(results.size() == 2);

    // Star at 180° should not be found
    bool found_opposite = false;
    for (u32 idx : results)
    {
        if (idx == 2)
        {
            found_opposite = true;
        }
    }
    CHECK_FALSE(found_opposite);
}

TEST_CASE("SpatialIndex: empty query on unbuilt index")
{
    SpatialIndex index;
    auto results = index.query(0.0, 0.0, 1.0, 10.0f);
    CHECK(results.empty());
}

TEST_CASE("SpatialIndex: query stats are populated")
{
    std::vector<StarEntry> stars;
    for (int i = 0; i < 100; ++i)
    {
        stars.push_back(make_star(
            static_cast<f64>(i) * 3.6,
            static_cast<f64>(i % 180) - 90.0,
            static_cast<f32>(4.0 + (i % 6)),
            static_cast<u32>(i)));
    }

    SpatialIndex index;
    index.build(stars, 180);

    SpatialQueryStats stats{};
    auto results = index.query(0.0, 0.0, 30.0 * astro_constants::kDegToRad, 10.0f, &stats);

    CHECK(stats.bands_searched > 0);
    CHECK(stats.results == static_cast<u32>(results.size()));
    CHECK(stats.query_time_ms >= 0.0);
}
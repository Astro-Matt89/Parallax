/// @file test_coordinates.cpp
/// @brief Unit tests for parallax::astro::Coordinates.
///
/// Verifies equatorial-to-horizontal transforms, inverse round-trips,
/// and stereographic screen projection against known reference values.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "astro/coordinates.hpp"
#include "astro/time_system.hpp"
#include "core/types.hpp"

#include <cmath>

using namespace parallax;
using namespace parallax::astro;

// =================================================================
// Tolerance constants
// =================================================================

/// 1 arcminute in radians — loose tolerance for approximate checks
static constexpr f64 kArcMinRad = astro_constants::kDegToRad / 60.0;

/// 1 arcsecond in radians — tight tolerance for precision checks
static constexpr f64 kArcSecRad = astro_constants::kArcSecToRad;

/// Degree tolerance for general angular comparisons
static constexpr f64 kDegTol = 0.5 * astro_constants::kDegToRad;

// =================================================================
// Equatorial → Horizontal tests
// =================================================================

TEST_CASE("Polaris near zenith from North Pole")
{
    // Polaris: RA ≈ 02h 31m 49s = 37.954°, Dec ≈ +89°15'51" ≈ +89.264°
    const EquatorialCoord polaris = {
        .ra  = 37.954 * astro_constants::kDegToRad,
        .dec = 89.264 * astro_constants::kDegToRad,
    };

    // Observer at North Pole
    const ObserverLocation north_pole = {
        .latitude_rad  = 90.0 * astro_constants::kDegToRad,
        .longitude_rad = 0.0,
    };

    // At the North Pole, altitude = declination for any LST
    const f64 lst = 0.0;  // LST doesn't matter for a pole observer + polar star

    const auto hz = Coordinates::equatorial_to_horizontal(polaris, north_pole, lst);

    // Altitude should be ≈ 89.264° (essentially at zenith)
    CHECK(hz.alt == doctest::Approx(89.264 * astro_constants::kDegToRad).epsilon(kArcMinRad));
}

TEST_CASE("Star transiting at zenith: RA=LST, Dec=Lat → alt≈90°")
{
    // If a star's RA equals the LST and its Dec equals the observer latitude,
    // it is transiting directly overhead (altitude ≈ 90°).
    const f64 lat = 45.0 * astro_constants::kDegToRad;
    const f64 lst = 6.0 * astro_constants::kHourToRad;  // 6h = 90°

    const EquatorialCoord eq = {
        .ra  = lst,   // RA = LST → hour angle = 0
        .dec = lat,   // Dec = latitude
    };

    const ObserverLocation observer = {
        .latitude_rad  = lat,
        .longitude_rad = 0.0,
    };

    const auto hz = Coordinates::equatorial_to_horizontal(eq, observer, lst);

    // Altitude should be 90° (zenith)
    CHECK(hz.alt == doctest::Approx(astro_constants::kHalfPi).epsilon(kArcSecRad));
}

TEST_CASE("Star on celestial equator due south at transit")
{
    // Observer at lat 45°N, star with Dec=0 at transit (HA=0)
    // Expected altitude = 90° - |lat| = 45°
    // Expected azimuth = 180° (due south)
    const f64 lat = 45.0 * astro_constants::kDegToRad;
    const f64 lst = 3.0 * astro_constants::kHourToRad;  // 45°

    const EquatorialCoord eq = {
        .ra  = lst,   // RA = LST → transit
        .dec = 0.0,   // Celestial equator
    };

    const ObserverLocation observer = {
        .latitude_rad  = lat,
        .longitude_rad = 0.0,
    };

    const auto hz = Coordinates::equatorial_to_horizontal(eq, observer, lst);

    // Altitude = 90° - 45° = 45°
    CHECK(hz.alt == doctest::Approx(45.0 * astro_constants::kDegToRad).epsilon(kArcSecRad));

    // Azimuth = 180° (due south)
    CHECK(hz.az == doctest::Approx(astro_constants::kPi).epsilon(kDegTol));
}

TEST_CASE("Star below horizon has negative altitude")
{
    // Circumpolar check: star at Dec = -60° from lat 45°N should be below horizon
    // when it transits (HA = 0): alt = 90° - |lat - dec| = 90° - 105° = -15°
    const f64 lat = 45.0 * astro_constants::kDegToRad;
    const f64 lst = 0.0;

    const EquatorialCoord eq = {
        .ra  = lst,
        .dec = -60.0 * astro_constants::kDegToRad,
    };

    const ObserverLocation observer = {
        .latitude_rad  = lat,
        .longitude_rad = 0.0,
    };

    const auto hz = Coordinates::equatorial_to_horizontal(eq, observer, lst);

    CHECK(hz.alt < 0.0);
}

TEST_CASE("Altitude is always in [-π/2, π/2]")
{
    const ObserverLocation observer = {
        .latitude_rad  = 30.0 * astro_constants::kDegToRad,
        .longitude_rad = 0.0,
    };

    const f64 lst = 12.0 * astro_constants::kHourToRad;

    // Test a range of RA/Dec values
    for (f64 ra_deg = 0.0; ra_deg < 360.0; ra_deg += 45.0)
    {
        for (f64 dec_deg = -80.0; dec_deg <= 80.0; dec_deg += 40.0)
        {
            const EquatorialCoord eq = {
                .ra  = ra_deg * astro_constants::kDegToRad,
                .dec = dec_deg * astro_constants::kDegToRad,
            };

            const auto hz = Coordinates::equatorial_to_horizontal(eq, observer, lst);

            CHECK(hz.alt >= -astro_constants::kHalfPi - 1e-10);
            CHECK(hz.alt <=  astro_constants::kHalfPi + 1e-10);
            CHECK(hz.az  >= 0.0);
            CHECK(hz.az  <  astro_constants::kTwoPi + 1e-10);
        }
    }
}

// =================================================================
// Round-trip: Equatorial → Horizontal → Equatorial
// =================================================================

TEST_CASE("Round-trip: eq → hz → eq preserves RA/Dec")
{
    const ObserverLocation observer = {
        .latitude_rad  = 51.48 * astro_constants::kDegToRad,  // London
        .longitude_rad = -0.0077 * astro_constants::kDegToRad,
    };

    const f64 lst = 18.0 * astro_constants::kHourToRad;

    // Vega: RA = 279.235°, Dec = +38.784°
    const EquatorialCoord original = {
        .ra  = 279.235 * astro_constants::kDegToRad,
        .dec = 38.784 * astro_constants::kDegToRad,
    };

    const auto hz = Coordinates::equatorial_to_horizontal(original, observer, lst);
    const auto result = Coordinates::horizontal_to_equatorial(hz, observer, lst);

    // RA must match within 1 arcsecond
    // Handle wrap-around for RA near 0/360 boundary
    f64 ra_diff = std::abs(result.ra - original.ra);
    if (ra_diff > astro_constants::kPi)
    {
        ra_diff = astro_constants::kTwoPi - ra_diff;
    }
    CHECK(ra_diff < 10.0 * kArcSecRad);

    // Dec must match within 1 arcsecond
    CHECK(result.dec == doctest::Approx(original.dec).epsilon(10.0 * kArcSecRad));
}

TEST_CASE("Round-trip for multiple stars at different locations")
{
    struct TestCase
    {
        f64 ra_deg;
        f64 dec_deg;
        f64 lat_deg;
        f64 lst_hours;
    };

    const TestCase cases[] = {
        {101.287, -16.716, 30.67, 6.0},    // Sirius from McDonald Observatory
        {213.915,  19.182, -33.86, 14.0},   // Arcturus from Sydney
        {37.954,   89.264, 60.0, 0.0},      // Polaris from lat 60°N
        {310.358,  45.280, 45.0, 20.0},     // Deneb from lat 45°N
    };

    for (const auto& tc : cases)
    {
        const EquatorialCoord original = {
            .ra  = tc.ra_deg * astro_constants::kDegToRad,
            .dec = tc.dec_deg * astro_constants::kDegToRad,
        };

        const ObserverLocation observer = {
            .latitude_rad  = tc.lat_deg * astro_constants::kDegToRad,
            .longitude_rad = 0.0,
        };

        const f64 lst = tc.lst_hours * astro_constants::kHourToRad;

        const auto hz = Coordinates::equatorial_to_horizontal(original, observer, lst);

        // Only test round-trip if star is above horizon
        if (hz.alt < -10.0 * astro_constants::kDegToRad)
        {
            continue;
        }

        const auto result = Coordinates::horizontal_to_equatorial(hz, observer, lst);

        f64 ra_diff = std::abs(result.ra - original.ra);
        if (ra_diff > astro_constants::kPi)
        {
            ra_diff = astro_constants::kTwoPi - ra_diff;
        }
        CHECK(ra_diff < 10.0 * kArcSecRad);
        CHECK(result.dec == doctest::Approx(original.dec).epsilon(10.0 * kArcSecRad));
    }
}

// =================================================================
// Screen projection tests
// =================================================================

TEST_CASE("Star at camera center projects to (0, 0)")
{
    const HorizontalCoord pointing = {
        .alt = 45.0 * astro_constants::kDegToRad,
        .az  = 180.0 * astro_constants::kDegToRad,
    };

    const f64 fov = 60.0 * astro_constants::kDegToRad;

    const auto result = Coordinates::horizontal_to_screen(pointing, pointing, fov);

    REQUIRE(result.has_value());
    CHECK(result->x == doctest::Approx(0.0f).epsilon(0.001f));
    CHECK(result->y == doctest::Approx(0.0f).epsilon(0.001f));
}

TEST_CASE("Star far off screen returns nullopt")
{
    const HorizontalCoord pointing = {
        .alt = 45.0 * astro_constants::kDegToRad,
        .az  = 0.0,
    };

    // Star 90° away from pointing direction
    const HorizontalCoord star = {
        .alt = 45.0 * astro_constants::kDegToRad,
        .az  = 180.0 * astro_constants::kDegToRad,
    };

    const f64 fov = 60.0 * astro_constants::kDegToRad;

    const auto result = Coordinates::horizontal_to_screen(star, pointing, fov);

    CHECK_FALSE(result.has_value());
}

TEST_CASE("Star slightly to the right of center projects to positive x")
{
    const HorizontalCoord pointing = {
        .alt = 45.0 * astro_constants::kDegToRad,
        .az  = 180.0 * astro_constants::kDegToRad,
    };

    // Star 5° to the east (higher azimuth)
    const HorizontalCoord star = {
        .alt = 45.0 * astro_constants::kDegToRad,
        .az  = 185.0 * astro_constants::kDegToRad,
    };

    const f64 fov = 60.0 * astro_constants::kDegToRad;

    const auto result = Coordinates::horizontal_to_screen(star, pointing, fov);

    REQUIRE(result.has_value());
    // 5° offset in a 60° FOV should be well within screen
    CHECK(result->x > 0.0f);     // East → positive x
    CHECK(result->x < 1.0f);     // Within bounds
    CHECK(std::abs(result->y) < 0.1f);  // Near horizontal center
}

TEST_CASE("Star above pointing direction projects to positive y")
{
    const HorizontalCoord pointing = {
        .alt = 45.0 * astro_constants::kDegToRad,
        .az  = 180.0 * astro_constants::kDegToRad,
    };

    // Star 10° higher
    const HorizontalCoord star = {
        .alt = 55.0 * astro_constants::kDegToRad,
        .az  = 180.0 * astro_constants::kDegToRad,
    };

    const f64 fov = 60.0 * astro_constants::kDegToRad;

    const auto result = Coordinates::horizontal_to_screen(star, pointing, fov);

    REQUIRE(result.has_value());
    CHECK(result->y > 0.0f);     // Higher altitude → positive y
    CHECK(result->y < 1.0f);     // Within bounds
    CHECK(std::abs(result->x) < 0.01f);  // Same azimuth → near x center
}

TEST_CASE("Narrow FOV rejects stars at moderate angular distance")
{
    const HorizontalCoord pointing = {
        .alt = 45.0 * astro_constants::kDegToRad,
        .az  = 180.0 * astro_constants::kDegToRad,
    };

    // Star 5° away — should be off screen with a 2° FOV
    const HorizontalCoord star = {
        .alt = 50.0 * astro_constants::kDegToRad,
        .az  = 180.0 * astro_constants::kDegToRad,
    };

    const f64 narrow_fov = 2.0 * astro_constants::kDegToRad;

    const auto result = Coordinates::horizontal_to_screen(star, pointing, narrow_fov);

    CHECK_FALSE(result.has_value());
}

TEST_CASE("Screen projection symmetric: equal offsets left/right give equal magnitude")
{
    const HorizontalCoord pointing = {
        .alt = 45.0 * astro_constants::kDegToRad,
        .az  = 180.0 * astro_constants::kDegToRad,
    };

    const f64 fov = 60.0 * astro_constants::kDegToRad;
    const f64 offset = 10.0 * astro_constants::kDegToRad;

    const HorizontalCoord star_east = {
        .alt = pointing.alt,
        .az  = pointing.az + offset,
    };
    const HorizontalCoord star_west = {
        .alt = pointing.alt,
        .az  = pointing.az - offset,
    };

    const auto result_east = Coordinates::horizontal_to_screen(star_east, pointing, fov);
    const auto result_west = Coordinates::horizontal_to_screen(star_west, pointing, fov);

    REQUIRE(result_east.has_value());
    REQUIRE(result_west.has_value());

    // Magnitudes should be approximately equal
    CHECK(std::abs(result_east->x) == doctest::Approx(std::abs(result_west->x)).epsilon(0.01f));

    // Signs should be opposite
    CHECK(result_east->x > 0.0f);
    CHECK(result_west->x < 0.0f);
}

// =================================================================
// Integration: full pipeline RA/Dec → Alt/Az → Screen
// =================================================================

TEST_CASE("Full pipeline: Polaris visible from lat 45°N looking north/up")
{
    // Polaris
    const EquatorialCoord polaris = {
        .ra  = 37.954 * astro_constants::kDegToRad,
        .dec = 89.264 * astro_constants::kDegToRad,
    };

    const ObserverLocation observer = {
        .latitude_rad  = 45.0 * astro_constants::kDegToRad,
        .longitude_rad = 0.0,
    };

    // LST chosen so Polaris is above horizon (always is from 45°N)
    const f64 lst = 37.954 * astro_constants::kDegToRad;

    const auto hz = Coordinates::equatorial_to_horizontal(polaris, observer, lst);

    // Polaris should be at altitude ≈ latitude ≈ 45° from lat 45°N
    CHECK(hz.alt > 0.0);
    CHECK(hz.alt == doctest::Approx(45.0 * astro_constants::kDegToRad).epsilon(2.0 * astro_constants::kDegToRad));

    // Point camera roughly at Polaris
    const HorizontalCoord pointing = {
        .alt = hz.alt,
        .az  = hz.az,
    };

    const f64 fov = 60.0 * astro_constants::kDegToRad;

    const auto screen = Coordinates::horizontal_to_screen(hz, pointing, fov);

    REQUIRE(screen.has_value());
    // Should be near screen center since we're pointing right at it
    CHECK(std::abs(screen->x) < 0.1f);
    CHECK(std::abs(screen->y) < 0.1f);
}
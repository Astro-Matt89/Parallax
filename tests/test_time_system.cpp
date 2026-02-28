/// @file test_time_system.cpp
/// @brief Unit tests for parallax::astro::TimeSystem.
///
/// Verifies Julian Date conversion (Meeus algorithm), GMST (IAU 1982),
/// LMST, and round-trip consistency against known reference values.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "astro/time_system.hpp"
#include "core/types.hpp"

#include <cmath>

using namespace parallax;
using namespace parallax::astro;

// =================================================================
// Helper: approximate equality for floating-point comparisons
// =================================================================

static constexpr f64 kJdTolerance     = 1e-6;   // ~0.086 seconds
static constexpr f64 kAngleTolDeg     = 0.01;   // Degrees
static constexpr f64 kSecondTolerance = 1.0;     // 1 second (for round-trip)

// =================================================================
// Julian Date conversion tests
// =================================================================

TEST_CASE("J2000.0 epoch gives JD 2451545.0")
{
    const DateTime j2000 = {
        .year   = 2000,
        .month  = 1,
        .day    = 1,
        .hour   = 12,
        .minute = 0,
        .second = 0.0,
    };

    const f64 jd = TimeSystem::to_julian_date(j2000);
    CHECK(jd == doctest::Approx(2451545.0).epsilon(kJdTolerance));
}

TEST_CASE("Known date: 1999-01-01 00:00 UTC → JD 2451179.5")
{
    const DateTime dt = {
        .year   = 1999,
        .month  = 1,
        .day    = 1,
        .hour   = 0,
        .minute = 0,
        .second = 0.0,
    };

    const f64 jd = TimeSystem::to_julian_date(dt);
    CHECK(jd == doctest::Approx(2451179.5).epsilon(kJdTolerance));
}

TEST_CASE("Known date: 2024-06-15 22:30:00 UTC")
{
    // Reference value from USNO Julian Date converter
    const DateTime dt = {
        .year   = 2024,
        .month  = 6,
        .day    = 15,
        .hour   = 22,
        .minute = 30,
        .second = 0.0,
    };

    const f64 jd = TimeSystem::to_julian_date(dt);
    // Expected: JD 2460476.4375
    CHECK(jd == doctest::Approx(2460476.4375).epsilon(kJdTolerance));
}

TEST_CASE("Historical date: Sputnik launch 1957-10-04 19:28:34 UTC")
{
    const DateTime dt = {
        .year   = 1957,
        .month  = 10,
        .day    = 4,
        .hour   = 19,
        .minute = 28,
        .second = 34.0,
    };

    const f64 jd = TimeSystem::to_julian_date(dt);
    // Expected: JD 2436116.3115... (approximately)
    CHECK(jd == doctest::Approx(2436116.31150).epsilon(1e-4));
}

// =================================================================
// Round-trip: DateTime → JD → DateTime
// =================================================================

TEST_CASE("Round-trip: DateTime → JD → DateTime preserves values")
{
    const DateTime original = {
        .year   = 2024,
        .month  = 3,
        .day    = 15,
        .hour   = 14,
        .minute = 30,
        .second = 45.0,
    };

    const f64 jd = TimeSystem::to_julian_date(original);
    const DateTime result = TimeSystem::from_julian_date(jd);

    CHECK(result.year   == original.year);
    CHECK(result.month  == original.month);
    CHECK(result.day    == original.day);
    CHECK(result.hour   == original.hour);
    CHECK(result.minute == original.minute);
    CHECK(result.second == doctest::Approx(original.second).epsilon(kSecondTolerance));
}

TEST_CASE("Round-trip: J2000.0 epoch")
{
    const DateTime original = {
        .year   = 2000,
        .month  = 1,
        .day    = 1,
        .hour   = 12,
        .minute = 0,
        .second = 0.0,
    };

    const f64 jd = TimeSystem::to_julian_date(original);
    const DateTime result = TimeSystem::from_julian_date(jd);

    CHECK(result.year   == 2000);
    CHECK(result.month  == 1);
    CHECK(result.day    == 1);
    CHECK(result.hour   == 12);
    CHECK(result.minute == 0);
    CHECK(result.second == doctest::Approx(0.0).epsilon(kSecondTolerance));
}

TEST_CASE("Round-trip: January date (month <= 2 branch)")
{
    const DateTime original = {
        .year   = 2025,
        .month  = 2,
        .day    = 14,
        .hour   = 8,
        .minute = 15,
        .second = 30.0,
    };

    const f64 jd = TimeSystem::to_julian_date(original);
    const DateTime result = TimeSystem::from_julian_date(jd);

    CHECK(result.year   == original.year);
    CHECK(result.month  == original.month);
    CHECK(result.day    == original.day);
    CHECK(result.hour   == original.hour);
    CHECK(result.minute == original.minute);
    CHECK(result.second == doctest::Approx(original.second).epsilon(kSecondTolerance));
}

// =================================================================
// Julian centuries
// =================================================================

TEST_CASE("Julian centuries at J2000.0 is 0.0")
{
    const f64 t = TimeSystem::julian_centuries(astro_constants::kJ2000);
    CHECK(t == doctest::Approx(0.0).epsilon(1e-12));
}

TEST_CASE("Julian centuries at J2100.0")
{
    // J2100.0 = JD 2488070.0 (2100-01-01 12:00 UTC, ~1.0 century)
    const DateTime dt = {
        .year   = 2100,
        .month  = 1,
        .day    = 1,
        .hour   = 12,
        .minute = 0,
        .second = 0.0,
    };
    const f64 jd = TimeSystem::to_julian_date(dt);
    const f64 t = TimeSystem::julian_centuries(jd);
    CHECK(t == doctest::Approx(1.0).epsilon(0.001));
}

// =================================================================
// GMST tests
// =================================================================

TEST_CASE("GMST at J2000.0 ≈ 280.46° (≈ 4.8949 rad)")
{
    const f64 gmst_rad = TimeSystem::gmst(astro_constants::kJ2000);
    const f64 gmst_deg = gmst_rad * astro_constants::kRadToDeg;

    // IAU 1982 formula gives exactly 280.46061837° at J2000.0
    CHECK(gmst_deg == doctest::Approx(280.46).epsilon(kAngleTolDeg));
}

TEST_CASE("GMST is in range [0, 2π)")
{
    // Test across several dates
    const f64 dates[] = {
        2451545.0,   // J2000.0
        2460000.0,   // ~2023
        2460476.0,   // ~2024-06
        2440587.5,   // Unix epoch
    };

    for (const f64 jd : dates)
    {
        const f64 gmst_rad = TimeSystem::gmst(jd);
        CHECK(gmst_rad >= 0.0);
        CHECK(gmst_rad < astro_constants::kTwoPi);
    }
}

// =================================================================
// LMST tests
// =================================================================

TEST_CASE("LMST at Greenwich (longitude 0) equals GMST")
{
    const f64 jd = astro_constants::kJ2000;
    const f64 gmst_val = TimeSystem::gmst(jd);
    const f64 lmst_val = TimeSystem::lmst(jd, 0.0);

    CHECK(lmst_val == doctest::Approx(gmst_val).epsilon(1e-12));
}

TEST_CASE("LMST shifts east by longitude")
{
    const f64 jd = astro_constants::kJ2000;
    const f64 lon = 15.0 * astro_constants::kDegToRad;  // 15° East = 1 hour

    const f64 gmst_val = TimeSystem::gmst(jd);
    const f64 lmst_val = TimeSystem::lmst(jd, lon);

    // LMST should be GMST + 15° (mod 2π)
    f64 expected = std::fmod(gmst_val + lon, astro_constants::kTwoPi);
    if (expected < 0.0)
    {
        expected += astro_constants::kTwoPi;
    }

    CHECK(lmst_val == doctest::Approx(expected).epsilon(1e-10));
}

TEST_CASE("LMST is in range [0, 2π)")
{
    // Test with a western longitude (negative)
    const f64 jd = 2460000.0;
    const f64 lon = -104.02 * astro_constants::kDegToRad;  // McDonald Observatory

    const f64 lmst_val = TimeSystem::lmst(jd, lon);
    CHECK(lmst_val >= 0.0);
    CHECK(lmst_val < astro_constants::kTwoPi);
}

// =================================================================
// now_as_jd sanity check
// =================================================================

TEST_CASE("now_as_jd returns a reasonable Julian Date")
{
    const f64 jd = TimeSystem::now_as_jd();

    // Should be after 2020-01-01 (JD ~2458849.5) and before 2100
    CHECK(jd > 2458849.5);
    CHECK(jd < 2488070.0);
}
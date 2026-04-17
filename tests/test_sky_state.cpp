/// @file test_sky_state.cpp
/// @brief Unit tests for SkyConditionCalculator — Task 6.4 (Sky Condition Calculator).
///
/// Design note: sky state boundaries are tested directly via the public
/// SkyConditionCalculator::classify() helper, which avoids constructing full
/// CelestialBodyState / Atmosphere objects for threshold verification.
/// Moon-contribution and full compute() tests use Atmosphere(Bortle=1)
/// to get predictable, well-known base values.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "astro/sky_state.hpp"
#include "astro/atmosphere.hpp"
#include "astro/solar_system.hpp"
#include "astro/time_system.hpp"
#include "astro/coordinates.hpp"
#include "core/types.hpp"

#include <cmath>
#include <array>

using namespace parallax;
using namespace parallax::astro;

// =================================================================
// Helper: construct a Bortle-1 (best dark sky) atmosphere
// =================================================================

static Atmosphere make_dark()
{
    return Atmosphere(AtmosphereParams{
        .pressure_mbar    = 1013.25f,
        .temperature_c    = 15.0f,
        .extinction_coeff = 0.20f,
        .bortle_scale     = 1.0f,
    });
}

// =================================================================
// Helper: build a CelestialBodyState with the given equatorial coords
// and illumination, leaving all other fields at zero/default.
// =================================================================

static CelestialBodyState make_body(f64 ra_rad, f64 dec_rad, f32 illumination = 0.0f)
{
    return CelestialBodyState{
        .equatorial           = {ra_rad, dec_rad},
        .horizontal           = {0.0, 0.0},
        .distance_au          = 1.0,
        .magnitude            = 0.0f,
        .angular_diameter_arcsec = 0.0f,
        .phase_angle_deg      = 0.0f,
        .illumination         = illumination,
    };
}

// =================================================================
// Helper: place a body at a specific altitude at 0°N/0°E by choosing
// appropriate RA given LST=0.
//
// At LST=0, observer lat=0: alt = arcsin(sin(lat)*sin(dec) + cos(lat)*cos(dec)*cos(H))
//           With lat=0 and H=LST-RA=0-RA: alt = arcsin(cos(dec)*cos(RA))
// So to reach a desired altitude (in degrees) with minimal RA:
//   choose dec=0, RA so that cos(RA) = sin(alt_deg * pi/180).
// This works for |alt_deg| <= 90.
// =================================================================

static CelestialBodyState body_at_altitude(f32 alt_deg, f32 illumination = 0.0f)
{
    const f64 alt_rad = static_cast<f64>(alt_deg) * astro_constants::kDegToRad;
    // dec=0, H = arccos(sin(alt_rad)) → RA = -H (place body east of meridian)
    // H = LST - RA = 0 - RA → RA = -H
    // sin(alt) = cos(dec)*cos(H) = cos(H), so H = arccos(sin(alt_rad))
    const f64 H   = std::acos(std::sin(alt_rad));
    const f64 ra  = -H;  // negative = body east of meridian, rising
    return make_body(ra, 0.0, illumination);
}

// =================================================================
// Observer at 0°N, 0°E; LST = 0
// =================================================================

static const ObserverLocation kOriginObserver{.latitude_rad = 0.0, .longitude_rad = 0.0};
static constexpr f64 kZeroLst = 0.0;

// =================================================================
// TEST 1: Sky state boundaries via classify()
// =================================================================

TEST_CASE("classify: -19° → Night")
{
    CHECK(SkyConditionCalculator::classify(-19.0f) == SkyState::Night);
}

TEST_CASE("classify: -18° → AstroTwilight (boundary belongs to brighter state)")
{
    CHECK(SkyConditionCalculator::classify(-18.0f) == SkyState::AstroTwilight);
}

TEST_CASE("classify: -13° → AstroTwilight")
{
    CHECK(SkyConditionCalculator::classify(-13.0f) == SkyState::AstroTwilight);
}

TEST_CASE("classify: -12° → NauticalTwilight (boundary belongs to brighter state)")
{
    CHECK(SkyConditionCalculator::classify(-12.0f) == SkyState::NauticalTwilight);
}

TEST_CASE("classify: -7° → NauticalTwilight")
{
    CHECK(SkyConditionCalculator::classify(-7.0f) == SkyState::NauticalTwilight);
}

TEST_CASE("classify: -6° → CivilTwilight (boundary belongs to brighter state)")
{
    CHECK(SkyConditionCalculator::classify(-6.0f) == SkyState::CivilTwilight);
}

TEST_CASE("classify: -1° → CivilTwilight")
{
    CHECK(SkyConditionCalculator::classify(-1.0f) == SkyState::CivilTwilight);
}

TEST_CASE("classify: 0° → Day (horizon boundary belongs to Day per USNO)")
{
    CHECK(SkyConditionCalculator::classify(0.0f) == SkyState::Day);
}

TEST_CASE("classify: +10° → Day")
{
    CHECK(SkyConditionCalculator::classify(10.0f) == SkyState::Day);
}

// =================================================================
// TEST 2: sky_state_name returns non-empty for every enum value
// =================================================================

TEST_CASE("sky_state_name: non-empty for all SkyState values")
{
    const std::array<SkyState, 5> all_states = {
        SkyState::Day,
        SkyState::CivilTwilight,
        SkyState::NauticalTwilight,
        SkyState::AstroTwilight,
        SkyState::Night,
    };
    for (const auto s : all_states)
    {
        CHECK_FALSE(sky_state_name(s).empty());
    }
}

// =================================================================
// TEST 3: Night with Moon below horizon → zero Moon contribution
// =================================================================

TEST_CASE("Night with Moon below horizon: zero moon delta, lm = base zenith lm")
{
    const auto atm = make_dark();

    // Sun well below −18° (Night)
    const auto sun  = body_at_altitude(-45.0f);
    // Moon below horizon
    const auto moon = body_at_altitude(-20.0f, 1.0f);  // full illumination, but below horizon

    const auto sc = SkyConditionCalculator::compute(sun, moon, kOriginObserver, kZeroLst, atm);

    CHECK(sc.state == SkyState::Night);
    CHECK(sc.moon_sky_brightness_delta == doctest::Approx(0.0f).epsilon(1e-6f));

    // Without moon contribution or twilight penalty, lm == atmosphere zenith lm
    const f32 expected_lm = atm.limiting_magnitude(astro_constants::kHalfPi);
    CHECK(sc.effective_limiting_mag == doctest::Approx(expected_lm).epsilon(1e-3f));
}

// =================================================================
// TEST 4: Night with full Moon at zenith
// =================================================================

TEST_CASE("Night with full Moon at zenith: delta ≈ 2.5, lm drops by 2.5")
{
    const auto atm = make_dark();

    // Sun well below −18°
    const auto sun  = body_at_altitude(-45.0f);
    // Moon at zenith: alt=90°, illumination=1.0
    const auto moon = body_at_altitude(90.0f, 1.0f);

    const auto sc = SkyConditionCalculator::compute(sun, moon, kOriginObserver, kZeroLst, atm);

    CHECK(sc.state == SkyState::Night);
    CHECK(sc.moon_sky_brightness_delta == doctest::Approx(2.5f).epsilon(0.05f));

    // Limiting mag drops by 2.5
    const f32 base_lm = atm.limiting_magnitude(astro_constants::kHalfPi);
    CHECK(sc.effective_limiting_mag == doctest::Approx(base_lm - 2.5f).epsilon(0.05f));

    // Sky brightness (zenith) drops by 2.5 mag/arcsec² (numerically smaller)
    const f32 base_sb = atm.sky_brightness_zenith();
    CHECK(sc.sky_brightness_zenith == doctest::Approx(base_sb - 2.5f).epsilon(0.05f));
}

// =================================================================
// TEST 5: Night with quarter Moon at 30° alt
// =================================================================

TEST_CASE("Night with quarter Moon at 30° alt: delta ≈ 0.625")
{
    const auto atm = make_dark();

    const auto sun  = body_at_altitude(-45.0f);
    // Moon at 30°, illumination=0.5 (quarter)
    const auto moon = body_at_altitude(30.0f, 0.5f);

    const auto sc = SkyConditionCalculator::compute(sun, moon, kOriginObserver, kZeroLst, atm);

    // delta = 2.5 * 0.5 * sin(30°) = 2.5 * 0.5 * 0.5 = 0.625
    CHECK(sc.moon_sky_brightness_delta == doctest::Approx(0.625f).epsilon(0.05f));
}

// =================================================================
// TEST 6: Day — effective limiting magnitude very low
// =================================================================

TEST_CASE("Day (sun at +30°): effective_limiting_mag < -1.0")
{
    const auto atm = make_dark();

    const auto sun  = body_at_altitude(30.0f);
    const auto moon = body_at_altitude(-30.0f, 0.0f);

    const auto sc = SkyConditionCalculator::compute(sun, moon, kOriginObserver, kZeroLst, atm);

    CHECK(sc.state == SkyState::Day);
    CHECK(sc.effective_limiting_mag < -1.0f);
}

// =================================================================
// TEST 7: Civil twilight — limiting magnitude increases monotonically
//         as Sun descends from 0° toward -6°
// =================================================================

TEST_CASE("Civil twilight: lm increases monotonically as Sun descends")
{
    const auto atm = make_dark();

    const auto moon = body_at_altitude(-30.0f, 0.0f);  // no moon effect

    const std::array<f32, 4> sun_alts = {0.0f, -2.0f, -4.0f, -6.0f};

    f32 prev_lm = -1e9f;
    for (const f32 alt : sun_alts)
    {
        const auto sun = body_at_altitude(alt);
        const auto sc  = SkyConditionCalculator::compute(
            sun, moon, kOriginObserver, kZeroLst, atm);

        // At 0° it's Day; at -6° it's CivilTwilight boundary. Either way lm should rise.
        CHECK(sc.effective_limiting_mag > prev_lm);
        prev_lm = sc.effective_limiting_mag;
    }
}

// =================================================================
// TEST 8: Full integration — real date 2024-06-21 06:00 UTC at 0°N, 0°E
// =================================================================

TEST_CASE("Integration: 2024-06-21 06:00 UTC at 0°N/0°E (summer solstice dawn)")
{
    // JD for 2024-06-21 06:00:00 UTC
    const f64 jd = TimeSystem::to_julian_date({2024, 6, 21, 6, 0, 0.0});

    const ObserverLocation observer{.latitude_rad = 0.0, .longitude_rad = 0.0};
    const f64 lst = TimeSystem::lmst(jd, observer.longitude_rad);

    const auto sun  = SolarSystem::compute_sun(jd);
    const auto moon_full = SolarSystem::compute_moon_full(jd);
    const auto moon = moon_full.body;

    const auto atm = make_dark();
    const auto sc  = SkyConditionCalculator::compute(sun, moon, observer, lst, atm);

    // At the equator on summer solstice at 06:00 UTC the sun is near the
    // eastern horizon — expect civil twilight or day (sun alt ≈ -2° to +5°).
    // Precomputed expected value: sun alt ≈ −1.9° (civil twilight, dawn).
    // Allow generous ±2° absolute tolerance to account for algorithm precision.
    CHECK(sc.sun_altitude_deg >= -3.9f);
    CHECK(sc.sun_altitude_deg <=  0.1f);

    // State should be CivilTwilight or Day (near horizon transition)
    const bool near_horizon =
        (sc.state == SkyState::CivilTwilight || sc.state == SkyState::Day);
    CHECK(near_horizon);

    // Moon altitude must be within valid range
    CHECK(sc.moon_altitude_deg >= -90.0f);
    CHECK(sc.moon_altitude_deg <=  90.0f);
}

// =================================================================
// TEST 9: Moon below horizon with full illumination → zero delta
// =================================================================

TEST_CASE("Moon below horizon (alt=-20°) with full illumination: delta = 0")
{
    const auto atm = make_dark();

    const auto sun  = body_at_altitude(-45.0f);
    const auto moon = body_at_altitude(-20.0f, 1.0f);

    const auto sc = SkyConditionCalculator::compute(sun, moon, kOriginObserver, kZeroLst, atm);

    CHECK(sc.moon_sky_brightness_delta == doctest::Approx(0.0f).epsilon(1e-6f));
}

// =================================================================
// TEST 10: Clamp safety — pathological illumination > 1.0 with Moon at zenith
// =================================================================

TEST_CASE("Clamp safety: illumination=1.5, moon at 90° → delta clamped to ≤ 3.5")
{
    const auto atm = make_dark();

    const auto sun  = body_at_altitude(-45.0f);
    // Deliberately pass over-range illumination (should be clamped)
    const auto moon = body_at_altitude(90.0f, 1.5f);

    const auto sc = SkyConditionCalculator::compute(sun, moon, kOriginObserver, kZeroLst, atm);

    CHECK(sc.moon_sky_brightness_delta <= 3.5f);
    CHECK(sc.moon_sky_brightness_delta >= 0.0f);

    // Must not crash, and effective_limiting_mag must be within valid clamp range
    CHECK(sc.effective_limiting_mag >= -10.0f);
    CHECK(sc.effective_limiting_mag <=   8.0f);
}

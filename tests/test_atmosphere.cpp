/// @file test_atmosphere.cpp
/// @brief Unit tests for parallax::astro::Atmosphere.
///
/// Verifies atmospheric refraction, airmass, extinction, sky brightness,
/// and limiting magnitude against published reference values.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "astro/atmosphere.hpp"
#include "core/types.hpp"

#include <cmath>

using namespace parallax;
using namespace parallax::astro;

// =================================================================
// Tolerance constants
// =================================================================

/// 1 arcminute in radians
static constexpr f64 kArcMinRad = astro_constants::kDegToRad / 60.0;

/// 1 arcsecond in radians
static constexpr f64 kArcSecRad = astro_constants::kArcSecToRad;

// =================================================================
// Helper: construct default atmosphere (standard conditions)
// =================================================================

static Atmosphere make_default()
{
    return Atmosphere(AtmosphereParams{
        .pressure_mbar = 1013.25f,
        .temperature_c = 15.0f,
        .extinction_coeff = 0.20f,
        .bortle_scale = 4.0f,
    });
}

// =================================================================
// Airmass tests (Rozenberg formula)
// =================================================================

TEST_CASE("Airmass at zenith is approximately 1.0")
{
    const auto atm = make_default();

    // Zenith = altitude 90° = π/2
    const f64 x = atm.airmass(astro_constants::kHalfPi);

    CHECK(x == doctest::Approx(1.0).epsilon(0.002));
}

TEST_CASE("Airmass at 30° altitude is approximately 2.0")
{
    const auto atm = make_default();

    // 30° altitude → zenith angle 60°
    const f64 alt_30 = 30.0 * astro_constants::kDegToRad;
    const f64 x = atm.airmass(alt_30);

    // Published reference: X ≈ 2.0 at z=60°
    CHECK(x == doctest::Approx(2.0).epsilon(0.05));
}

TEST_CASE("Airmass at 10° altitude is approximately 5.6")
{
    const auto atm = make_default();

    const f64 alt_10 = 10.0 * astro_constants::kDegToRad;
    const f64 x = atm.airmass(alt_10);

    // Published reference: X ≈ 5.6 at z=80°
    CHECK(x == doctest::Approx(5.6).epsilon(0.2));
}

TEST_CASE("Airmass at horizon is very large (~38–40)")
{
    const auto atm = make_default();

    const f64 x = atm.airmass(0.0);

    // Rozenberg gives ~38–40 at the horizon
    CHECK(x > 35.0);
    CHECK(x < 45.0);
}

TEST_CASE("Airmass below horizon returns 40.0")
{
    const auto atm = make_default();

    const f64 x = atm.airmass(-10.0 * astro_constants::kDegToRad);

    CHECK(x == doctest::Approx(40.0));
}

TEST_CASE("Airmass is monotonically decreasing with altitude")
{
    const auto atm = make_default();

    f64 prev_x = atm.airmass(0.001);  // Just above horizon
    for (f64 alt_deg = 5.0; alt_deg <= 90.0; alt_deg += 5.0)
    {
        const f64 x = atm.airmass(alt_deg * astro_constants::kDegToRad);
        CHECK(x < prev_x);
        prev_x = x;
    }
}

// =================================================================
// Refraction tests (Bennett formula)
// =================================================================

TEST_CASE("Refraction at horizon is approximately 34 arcminutes")
{
    const auto atm = make_default();

    // At h = 0° (horizon)
    const f64 r = atm.refraction(0.0);

    // Published: ~34' at standard conditions
    const f64 r_arcmin = r / kArcMinRad;
    CHECK(r_arcmin == doctest::Approx(34.0).epsilon(2.0));
}

TEST_CASE("Refraction at 45° altitude is approximately 58 arcseconds")
{
    const auto atm = make_default();

    const f64 alt_45 = 45.0 * astro_constants::kDegToRad;
    const f64 r = atm.refraction(alt_45);

    // Published: ~58" at 45°
    const f64 r_arcsec = r / kArcSecRad;
    CHECK(r_arcsec == doctest::Approx(58.0).epsilon(5.0));
}

TEST_CASE("Refraction at zenith is nearly zero")
{
    const auto atm = make_default();

    const f64 r = atm.refraction(astro_constants::kHalfPi);

    // At zenith, refraction should be negligible (< 1 arcsecond)
    CHECK(r < kArcSecRad);
}

TEST_CASE("Refraction at 10° altitude is approximately 5.3 arcminutes")
{
    const auto atm = make_default();

    const f64 alt_10 = 10.0 * astro_constants::kDegToRad;
    const f64 r = atm.refraction(alt_10);

    // Published: ~5.3' at 10°
    const f64 r_arcmin = r / kArcMinRad;
    CHECK(r_arcmin == doctest::Approx(5.3).epsilon(0.5));
}

TEST_CASE("Refraction is always non-negative")
{
    const auto atm = make_default();

    for (f64 alt_deg = -10.0; alt_deg <= 90.0; alt_deg += 5.0)
    {
        const f64 r = atm.refraction(alt_deg * astro_constants::kDegToRad);
        CHECK(r >= 0.0);
    }
}

TEST_CASE("Refraction below horizon clamps to horizon value")
{
    const auto atm = make_default();

    const f64 r_below = atm.refraction(-5.0 * astro_constants::kDegToRad);
    const f64 r_horizon = atm.refraction(0.0);

    // Below-horizon should return same as horizon
    CHECK(r_below == doctest::Approx(r_horizon).epsilon(1e-10));
}

TEST_CASE("Refraction is monotonically decreasing with altitude")
{
    const auto atm = make_default();

    f64 prev_r = atm.refraction(0.0);
    for (f64 alt_deg = 5.0; alt_deg <= 90.0; alt_deg += 5.0)
    {
        const f64 r = atm.refraction(alt_deg * astro_constants::kDegToRad);
        CHECK(r <= prev_r);
        prev_r = r;
    }
}

TEST_CASE("Refraction scales with pressure and temperature")
{
    // Higher pressure → more refraction
    const Atmosphere atm_high_p(AtmosphereParams{
        .pressure_mbar = 1050.0f,
        .temperature_c = 15.0f,
        .extinction_coeff = 0.20f,
        .bortle_scale = 4.0f,
    });

    const Atmosphere atm_low_p(AtmosphereParams{
        .pressure_mbar = 900.0f,
        .temperature_c = 15.0f,
        .extinction_coeff = 0.20f,
        .bortle_scale = 4.0f,
    });

    const f64 alt_20 = 20.0 * astro_constants::kDegToRad;
    CHECK(atm_high_p.refraction(alt_20) > atm_low_p.refraction(alt_20));

    // Lower temperature → more refraction (denser air)
    const Atmosphere atm_cold(AtmosphereParams{
        .pressure_mbar = 1013.25f,
        .temperature_c = -10.0f,
        .extinction_coeff = 0.20f,
        .bortle_scale = 4.0f,
    });

    const Atmosphere atm_warm(AtmosphereParams{
        .pressure_mbar = 1013.25f,
        .temperature_c = 30.0f,
        .extinction_coeff = 0.20f,
        .bortle_scale = 4.0f,
    });

    CHECK(atm_cold.refraction(alt_20) > atm_warm.refraction(alt_20));
}

// =================================================================
// Extinction tests
// =================================================================

TEST_CASE("Extinction at zenith with k=0.2 is approximately 0.2 mag")
{
    const auto atm = make_default();

    const f32 ext = atm.extinction_mag(astro_constants::kHalfPi);

    // At zenith: X ≈ 1.0, so Δm = 0.20 × 1.0 = 0.20
    CHECK(ext == doctest::Approx(0.20f).epsilon(0.01f));
}

TEST_CASE("Extinction at 30° with k=0.2 is approximately 0.4 mag")
{
    const auto atm = make_default();

    const f64 alt_30 = 30.0 * astro_constants::kDegToRad;
    const f32 ext = atm.extinction_mag(alt_30);

    // At 30°: X ≈ 2.0, so Δm ≈ 0.20 × 2.0 = 0.40
    CHECK(ext == doctest::Approx(0.40f).epsilon(0.05f));
}

TEST_CASE("Extinction at 10° with k=0.2 is approximately 1.1 mag")
{
    const auto atm = make_default();

    const f64 alt_10 = 10.0 * astro_constants::kDegToRad;
    const f32 ext = atm.extinction_mag(alt_10);

    // At 10°: X ≈ 5.6, so Δm ≈ 0.20 × 5.6 = 1.12
    CHECK(ext == doctest::Approx(1.12f).epsilon(0.1f));
}

TEST_CASE("Extinction factor at zenith is close to 1.0")
{
    const auto atm = make_default();

    const f32 factor = atm.extinction_factor(astro_constants::kHalfPi);

    // At zenith with k=0.2: factor = 10^(-0.4 × 0.2) = 10^(-0.08) ≈ 0.831
    CHECK(factor == doctest::Approx(0.831f).epsilon(0.01f));
}

TEST_CASE("Extinction factor is in (0, 1] range")
{
    const auto atm = make_default();

    for (f64 alt_deg = 0.5; alt_deg <= 90.0; alt_deg += 5.0)
    {
        const f32 factor = atm.extinction_factor(alt_deg * astro_constants::kDegToRad);
        CHECK(factor > 0.0f);
        CHECK(factor <= 1.0f);
    }
}

TEST_CASE("Higher extinction coefficient dims more")
{
    const Atmosphere atm_low_k(AtmosphereParams{
        .pressure_mbar = 1013.25f,
        .temperature_c = 15.0f,
        .extinction_coeff = 0.12f,
        .bortle_scale = 4.0f,
    });

    const Atmosphere atm_high_k(AtmosphereParams{
        .pressure_mbar = 1013.25f,
        .temperature_c = 15.0f,
        .extinction_coeff = 0.40f,
        .bortle_scale = 4.0f,
    });

    const f64 alt_30 = 30.0 * astro_constants::kDegToRad;

    CHECK(atm_high_k.extinction_mag(alt_30) > atm_low_k.extinction_mag(alt_30));
    CHECK(atm_high_k.extinction_factor(alt_30) < atm_low_k.extinction_factor(alt_30));
}

// =================================================================
// Sky brightness tests
// =================================================================

TEST_CASE("Sky brightness at zenith for Bortle 1 is approximately 22.0 mag/arcsec²")
{
    const Atmosphere atm(AtmosphereParams{
        .pressure_mbar = 1013.25f,
        .temperature_c = 15.0f,
        .extinction_coeff = 0.20f,
        .bortle_scale = 1.0f,
    });

    const f32 sb = atm.sky_brightness_zenith();
    CHECK(sb == doctest::Approx(22.0f).epsilon(0.1f));
}

TEST_CASE("Sky brightness at zenith for Bortle 9 is approximately 17.0 mag/arcsec²")
{
    const Atmosphere atm(AtmosphereParams{
        .pressure_mbar = 1013.25f,
        .temperature_c = 15.0f,
        .extinction_coeff = 0.20f,
        .bortle_scale = 9.0f,
    });

    const f32 sb = atm.sky_brightness_zenith();
    CHECK(sb == doctest::Approx(17.0f).epsilon(0.1f));
}

TEST_CASE("Sky brightness at zenith for Bortle 4 is approximately 20.1 mag/arcsec²")
{
    const auto atm = make_default();

    const f32 sb = atm.sky_brightness_zenith();

    // Bortle 4: 22.0 - (4-1) × 0.625 = 22.0 - 1.875 = 20.125
    CHECK(sb == doctest::Approx(20.125f).epsilon(0.1f));
}

TEST_CASE("Higher Bortle → lower (brighter) sky brightness number")
{
    for (f32 b = 1.0f; b < 9.0f; b += 1.0f)
    {
        const Atmosphere atm_low(AtmosphereParams{.bortle_scale = b});
        const Atmosphere atm_high(AtmosphereParams{.bortle_scale = b + 1.0f});

        // Lower mag/arcsec² = brighter sky = worse conditions
        CHECK(atm_high.sky_brightness_zenith() < atm_low.sky_brightness_zenith());
    }
}

// =================================================================
// Limiting magnitude tests
// =================================================================

TEST_CASE("Limiting magnitude at zenith for Bortle 1 is approximately 7.8")
{
    const Atmosphere atm(AtmosphereParams{
        .pressure_mbar = 1013.25f,
        .temperature_c = 15.0f,
        .extinction_coeff = 0.20f,
        .bortle_scale = 1.0f,
    });

    const f32 m_lim = atm.limiting_magnitude(astro_constants::kHalfPi);

    // At zenith, X ≈ 1.0, extinction penalty ≈ 0
    CHECK(m_lim == doctest::Approx(7.8f).epsilon(0.1f));
}

TEST_CASE("Limiting magnitude at zenith for Bortle 9 is approximately 3.8")
{
    const Atmosphere atm(AtmosphereParams{
        .pressure_mbar = 1013.25f,
        .temperature_c = 15.0f,
        .extinction_coeff = 0.20f,
        .bortle_scale = 9.0f,
    });

    const f32 m_lim = atm.limiting_magnitude(astro_constants::kHalfPi);
    CHECK(m_lim == doctest::Approx(3.8f).epsilon(0.1f));
}

TEST_CASE("Limiting magnitude at zenith for Bortle 4 is approximately 6.3")
{
    const auto atm = make_default();

    const f32 m_lim = atm.limiting_magnitude(astro_constants::kHalfPi);

    // Bortle 4: 7.8 - (4-1) × 0.5 = 7.8 - 1.5 = 6.3
    CHECK(m_lim == doctest::Approx(6.3f).epsilon(0.1f));
}

TEST_CASE("Limiting magnitude decreases at lower altitudes")
{
    const auto atm = make_default();

    const f32 m_zenith = atm.limiting_magnitude(astro_constants::kHalfPi);
    const f32 m_30 = atm.limiting_magnitude(30.0 * astro_constants::kDegToRad);
    const f32 m_10 = atm.limiting_magnitude(10.0 * astro_constants::kDegToRad);

    // Fainter limit at zenith, progressively worse near horizon
    CHECK(m_zenith > m_30);
    CHECK(m_30 > m_10);
}

TEST_CASE("Limiting magnitude is monotonically increasing with altitude")
{
    const auto atm = make_default();

    f32 prev_m = atm.limiting_magnitude(1.0 * astro_constants::kDegToRad);
    for (f64 alt_deg = 5.0; alt_deg <= 90.0; alt_deg += 5.0)
    {
        const f32 m = atm.limiting_magnitude(alt_deg * astro_constants::kDegToRad);
        CHECK(m >= prev_m);
        prev_m = m;
    }
}

TEST_CASE("Higher Bortle → lower limiting magnitude")
{
    for (f32 b = 1.0f; b < 9.0f; b += 1.0f)
    {
        const Atmosphere atm_dark(AtmosphereParams{.bortle_scale = b});
        const Atmosphere atm_bright(AtmosphereParams{.bortle_scale = b + 1.0f});

        const f32 m_dark = atm_dark.limiting_magnitude(astro_constants::kHalfPi);
        const f32 m_bright = atm_bright.limiting_magnitude(astro_constants::kHalfPi);

        // Darker sky → can see fainter stars
        CHECK(m_dark > m_bright);
    }
}

// =================================================================
// Parameter mutation tests
// =================================================================

TEST_CASE("set_params updates atmospheric conditions")
{
    auto atm = make_default();

    const f32 sb_before = atm.sky_brightness_zenith();

    atm.set_params(AtmosphereParams{
        .pressure_mbar = 1013.25f,
        .temperature_c = 15.0f,
        .extinction_coeff = 0.20f,
        .bortle_scale = 9.0f,
    });

    const f32 sb_after = atm.sky_brightness_zenith();

    // Bortle 9 sky is much brighter (lower mag/arcsec²)
    CHECK(sb_after < sb_before);
}

TEST_CASE("get_params returns current parameters")
{
    const auto atm = make_default();

    const auto& params = atm.get_params();
    CHECK(params.pressure_mbar == doctest::Approx(1013.25f));
    CHECK(params.temperature_c == doctest::Approx(15.0f));
    CHECK(params.extinction_coeff == doctest::Approx(0.20f));
    CHECK(params.bortle_scale == doctest::Approx(4.0f));
}

// =================================================================
// Integration / cross-check tests
// =================================================================

TEST_CASE("Extinction at 30° matches k × airmass(30°)")
{
    const auto atm = make_default();

    const f64 alt_30 = 30.0 * astro_constants::kDegToRad;
    const f64 x = atm.airmass(alt_30);
    const f32 ext = atm.extinction_mag(alt_30);

    // ext should equal k × X
    const f32 expected = 0.20f * static_cast<f32>(x);
    CHECK(ext == doctest::Approx(expected).epsilon(0.001f));
}

TEST_CASE("Extinction factor is 10^(-0.4 × extinction_mag)")
{
    const auto atm = make_default();

    for (f64 alt_deg = 5.0; alt_deg <= 90.0; alt_deg += 15.0)
    {
        const f64 alt = alt_deg * astro_constants::kDegToRad;
        const f32 delta_m = atm.extinction_mag(alt);
        const f32 factor = atm.extinction_factor(alt);

        const f32 expected = static_cast<f32>(std::pow(10.0, -0.4 * static_cast<f64>(delta_m)));
        CHECK(factor == doctest::Approx(expected).epsilon(0.001f));
    }
}
/// @file test_image_formation.cpp
/// @brief Unit tests for ImageFormation (Sprint 10a Task 10a.4).
///
/// All tests use a lightweight stub IObjectSource that returns hand-crafted
/// CelestialObject lists without requiring real catalog data.
///
/// Test coverage:
///   - Single bright on-axis star lands near the detector centre.
///   - Increasing integration_time increases accumulated signal above background.
///   - Output MultispectralImage has one band per requested BandSpec with correct metadata.
///   - Fixed seed reproduces identical images across two form() calls (same params).
///   - Empty object source yields an image consistent with background + noise only.
///   - Projection sanity: RA offset → East (+x), Dec offset → North (−y).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "imaging/image_formation.hpp"
#include "imaging/image.hpp"
#include "imaging/multispectral_image.hpp"

#include "instruments/array_instrument.hpp"
#include "instruments/station.hpp"
#include "instruments/spectral_band.hpp"
#include "universe/celestial_object.hpp"
#include "universe/object_id.hpp"
#include "core/types.hpp"
#include "astro/observer.hpp"

#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

// ---------------------------------------------------------------------------
// Aliases
// ---------------------------------------------------------------------------

using parallax::f32;
using parallax::f64;
using parallax::u32;
using parallax::u64;

using parallax::imaging::BandSpec;
using parallax::imaging::IObjectSource;
using parallax::imaging::ImageFormation;
using parallax::imaging::ImageFormationParams;
using parallax::imaging::MultispectralImage;
using parallax::imaging::Image;

using parallax::instruments::ArrayInstrument;
using parallax::instruments::Station;
using parallax::instruments::SpectralBand;

using parallax::universe::CelestialObject;
using parallax::universe::ObjectType;
using parallax::astro::ParentBody;

// ---------------------------------------------------------------------------
// Stub IObjectSource
// ---------------------------------------------------------------------------

/// @brief Minimal stub that returns a fixed list of objects for any query.
class StubObjectSource final : public IObjectSource
{
public:
    explicit StubObjectSource(std::vector<CelestialObject> objects)
        : m_objects(std::move(objects))
    {
    }

    void query_fov(double  /*ra_rad*/,
                   double  /*dec_rad*/,
                   double  /*radius_deg*/,
                   float   /*mag_limit*/,
                   std::vector<CelestialObject>& results) const override
    {
        results = m_objects;
    }

private:
    std::vector<CelestialObject> m_objects;
};

// ---------------------------------------------------------------------------
// Test-fixture helpers
// ---------------------------------------------------------------------------

namespace
{
    /// Build a minimal single-station ArrayInstrument for testing.
    /// One Moon station, 1 m aperture, single Visible band.
    [[nodiscard]] ArrayInstrument make_test_instrument()
    {
        ArrayInstrument inst{0x0001u, "TestArray"};

        inst.add_station(Station{
            .name                = "Test Station",
            .body                = ParentBody::Moon,
            .latitude_rad        = 0.0,
            .longitude_rad       = 0.0,
            .elevation_m         = 0.0,
            .aperture_diameter_m = 1.0f,
            .efficiency          = 0.8f,
            .has_atmosphere      = false,
            .is_active           = true,
        });

        return inst;
    }

    /// Build a CelestialObject representing a star at (ra_rad, dec_rad) with
    /// V-magnitude @p mag_v.
    [[nodiscard]] CelestialObject make_star(double ra_rad, double dec_rad, float mag_v)
    {
        CelestialObject obj{};
        obj.type  = ObjectType::Star;
        obj.id    = parallax::universe::encode_id(ObjectType::Star, 1u);
        obj.ra    = ra_rad;
        obj.dec   = dec_rad;
        obj.mag_v = mag_v;
        return obj;
    }

    /// Default test band: Visible (550 nm, 200 nm bandwidth).
    [[nodiscard]] BandSpec make_visible_band()
    {
        return BandSpec{
            .band_name           = "Visible",
            .band_index          = 0,
            .center_wavelength_nm = 550.0,
            .bandwidth_nm        = 200.0,
        };
    }

    /// Sum all pixel values in an image.
    [[nodiscard]] f64 pixel_sum(const Image& img)
    {
        f64 total = 0.0;
        for (const f32 v : img.pixels())
        {
            total += static_cast<f64>(v);
        }
        return total;
    }

    /// Return the (x, y) coordinates of the brightest pixel.
    [[nodiscard]] std::pair<u32, u32> peak_coords(const Image& img)
    {
        u32 bx = 0, by = 0;
        f32 best = -std::numeric_limits<f32>::max();
        for (u32 y = 0; y < img.height(); ++y)
        {
            for (u32 x = 0; x < img.width(); ++x)
            {
                if (img(x, y) > best)
                {
                    best = img(x, y);
                    bx   = x;
                    by   = y;
                }
            }
        }
        return {bx, by};
    }
} // anonymous namespace

// ===========================================================================
// Test: band count and metadata
// ===========================================================================

TEST_CASE("form() returns one band per requested BandSpec with correct metadata")
{
    const ArrayInstrument inst = make_test_instrument();
    const StubObjectSource src{{}};  // No objects.

    ImageFormationParams params;
    params.ra_rad            = 0.0;
    params.dec_rad           = 0.0;
    params.fov_arcsec        = 60.0;
    params.width_px          = 32;
    params.height_px         = 32;
    params.integration_time_s = 1.0;
    params.seed              = 1u;
    params.bands             = {
        BandSpec{.band_name = "V",       .band_index = 0, .center_wavelength_nm = 550.0,  .bandwidth_nm = 200.0},
        BandSpec{.band_name = "Near-IR", .band_index = 1, .center_wavelength_nm = 1600.0, .bandwidth_nm = 400.0},
    };

    const MultispectralImage result = ImageFormation::form(params, inst, src);

    CHECK(result.band_count() == 2u);
    CHECK(result.width()      == 32u);
    CHECK(result.height()     == 32u);

    CHECK(result.band(0).metadata().band_name    == "V");
    CHECK(result.band(0).metadata().wavelength_nm == doctest::Approx(550.0));
    CHECK(result.band(0).metadata().band_index   == 0u);

    CHECK(result.band(1).metadata().band_name    == "Near-IR");
    CHECK(result.band(1).metadata().wavelength_nm == doctest::Approx(1600.0));
    CHECK(result.band(1).metadata().band_index   == 1u);

    // Pixel scale must equal fov / width.
    const f64 expected_scale = 60.0 / 32.0;
    CHECK(result.pixel_scale_arcsec_per_px() == doctest::Approx(expected_scale));
    CHECK(result.band(0).metadata().pixel_scale_arcsec_per_px == doctest::Approx(expected_scale));
}

// ===========================================================================
// Test: on-axis star peaks near detector centre
// ===========================================================================

TEST_CASE("Bright on-axis star produces a peak near the detector centre")
{
    const ArrayInstrument inst = make_test_instrument();

    // A magnitude-6 star placed exactly at the pointing centre.
    constexpr f64 kRa  = 1.0;
    constexpr f64 kDec = 0.3;
    const StubObjectSource src{{ make_star(kRa, kDec, 6.0f) }};

    ImageFormationParams params;
    params.ra_rad            = kRa;
    params.dec_rad           = kDec;
    params.fov_arcsec        = 60.0;
    params.width_px          = 64;
    params.height_px         = 64;
    params.integration_time_s = 0.01;   // short integration, modest signal
    params.sky_background_e_per_s_px = 0.0;  // disable background for clarity
    params.read_noise_electrons = 0.0;        // disable read noise for clarity
    params.seed              = 42u;
    params.bands             = { make_visible_band() };

    const MultispectralImage result = ImageFormation::form(params, inst, src);
    REQUIRE(result.band_count() == 1u);

    const auto [px, py] = peak_coords(result.band(0));

    // Peak should be within 2 pixels of the centre (31, 31 for a 64×64 image).
    const int centre = 31;
    CHECK(std::abs(static_cast<int>(px) - centre) <= 2);
    CHECK(std::abs(static_cast<int>(py) - centre) <= 2);
}

// ===========================================================================
// Test: longer integration → more signal
// ===========================================================================

TEST_CASE("Longer integration time yields more accumulated signal than shorter")
{
    const ArrayInstrument inst = make_test_instrument();

    constexpr f64 kRa  = 0.0;
    constexpr f64 kDec = 0.0;
    const StubObjectSource src{{ make_star(kRa, kDec, 10.0f) }};

    // Short exposure (1 s).
    ImageFormationParams short_params;
    short_params.ra_rad            = kRa;
    short_params.dec_rad           = kDec;
    short_params.fov_arcsec        = 60.0;
    short_params.width_px          = 32;
    short_params.height_px         = 32;
    short_params.integration_time_s = 1.0;
    short_params.sky_background_e_per_s_px = 10.0;
    short_params.read_noise_electrons = 3.0;
    short_params.seed              = 99u;
    short_params.bands             = { make_visible_band() };

    // Long exposure (100 s, same seed).
    ImageFormationParams long_params = short_params;
    long_params.integration_time_s  = 100.0;

    const MultispectralImage short_result = ImageFormation::form(short_params, inst, src);
    const MultispectralImage long_result  = ImageFormation::form(long_params,  inst, src);

    REQUIRE(short_result.band_count() == 1u);
    REQUIRE(long_result.band_count()  == 1u);

    // The total pixel sum should be larger for the longer exposure.
    // We compare sums (background + signal both scale with time, so the
    // longer image has a proportionally larger sum).
    const f64 short_sum = pixel_sum(short_result.band(0));
    const f64 long_sum  = pixel_sum(long_result.band(0));
    CHECK(long_sum > short_sum);
}

// ===========================================================================
// Test: reproducibility — same seed, same output
// ===========================================================================

TEST_CASE("Same seed produces identical images across two form() calls")
{
    const ArrayInstrument inst = make_test_instrument();

    constexpr f64 kRa  = 0.0;
    constexpr f64 kDec = 0.0;
    const StubObjectSource src{{ make_star(kRa, kDec, 12.0f) }};

    ImageFormationParams params;
    params.ra_rad            = kRa;
    params.dec_rad           = kDec;
    params.fov_arcsec        = 60.0;
    params.width_px          = 32;
    params.height_px         = 32;
    params.integration_time_s = 5.0;
    params.sky_background_e_per_s_px = 10.0;
    params.read_noise_electrons = 3.0;
    params.seed              = 1234u;
    params.bands             = { make_visible_band() };

    const MultispectralImage result_a = ImageFormation::form(params, inst, src);
    const MultispectralImage result_b = ImageFormation::form(params, inst, src);

    REQUIRE(result_a.band_count() == 1u);
    REQUIRE(result_b.band_count() == 1u);

    // Every pixel must be identical.
    for (u32 y = 0; y < params.height_px; ++y)
    {
        for (u32 x = 0; x < params.width_px; ++x)
        {
            CHECK(result_a.band(0)(x, y) == result_b.band(0)(x, y));
        }
    }
}

// ===========================================================================
// Test: empty object source → background + noise only
// ===========================================================================

TEST_CASE("Empty object source yields mean pixel value consistent with background")
{
    const ArrayInstrument inst = make_test_instrument();
    const StubObjectSource src{{}};  // No objects.

    constexpr f64 kBgRate = 50.0;   // e⁻/s/px
    constexpr f64 kT      = 10.0;   // s
    constexpr f64 kExpected = kBgRate * kT;  // 500 e⁻/px expected mean

    ImageFormationParams params;
    params.ra_rad            = 0.0;
    params.dec_rad           = 0.0;
    params.fov_arcsec        = 60.0;
    params.width_px          = 64;
    params.height_px         = 64;
    params.integration_time_s = kT;
    params.sky_background_e_per_s_px = kBgRate;
    params.read_noise_electrons = 0.0;  // Disable read noise; test pure background.
    params.seed              = 777u;
    params.bands             = { make_visible_band() };

    const MultispectralImage result = ImageFormation::form(params, inst, src);
    REQUIRE(result.band_count() == 1u);

    const f64 total   = pixel_sum(result.band(0));
    const f64 n_px    = static_cast<f64>(params.width_px) * params.height_px;
    const f64 mean_px = total / n_px;

    // Poisson noise on 500 counts: σ = sqrt(500) ≈ 22.  Allow ±10% tolerance.
    CHECK(mean_px == doctest::Approx(kExpected).epsilon(0.1));
}

// ===========================================================================
// Test: projection direction
// ===========================================================================

TEST_CASE("Object offset East in RA lands right of centre (+x)")
{
    const ArrayInstrument inst = make_test_instrument();

    constexpr f64 kRa0    = 1.0;
    constexpr f64 kDec0   = 0.0;
    // Small RA offset: 20 arcsec East.
    constexpr f64 kOffsetArcsec = 20.0;
    const f64 offset_rad = kOffsetArcsec * parallax::astro_constants::kArcSecToRad;

    // The offset object at ra0 + offset, dec0.
    const StubObjectSource src{{ make_star(kRa0 + offset_rad, kDec0, 6.0f) }};

    ImageFormationParams params;
    params.ra_rad            = kRa0;
    params.dec_rad           = kDec0;
    params.fov_arcsec        = 120.0;   // 120 arcsec FOV; pixel scale = 120/64 ≈ 1.875 "/px
    params.width_px          = 64;
    params.height_px         = 64;
    params.integration_time_s = 0.01;
    params.sky_background_e_per_s_px = 0.0;
    params.read_noise_electrons = 0.0;
    params.seed              = 55u;
    params.bands             = { make_visible_band() };

    const MultispectralImage result = ImageFormation::form(params, inst, src);
    REQUIRE(result.band_count() == 1u);

    const auto [px, py] = peak_coords(result.band(0));
    const int centre_x = static_cast<int>(params.width_px) / 2;

    // Object is East (positive RA offset at dec=0) → peak x > centre.
    CHECK(static_cast<int>(px) > centre_x);
}

TEST_CASE("Object offset North in Dec lands above centre (−y in image coords)")
{
    const ArrayInstrument inst = make_test_instrument();

    constexpr f64 kRa0  = 1.0;
    constexpr f64 kDec0 = 0.0;
    constexpr f64 kOffsetArcsec = 20.0;
    const f64 offset_rad = kOffsetArcsec * parallax::astro_constants::kArcSecToRad;

    // Object at dec0 + offset (North).
    const StubObjectSource src{{ make_star(kRa0, kDec0 + offset_rad, 6.0f) }};

    ImageFormationParams params;
    params.ra_rad            = kRa0;
    params.dec_rad           = kDec0;
    params.fov_arcsec        = 120.0;
    params.width_px          = 64;
    params.height_px         = 64;
    params.integration_time_s = 0.01;
    params.sky_background_e_per_s_px = 0.0;
    params.read_noise_electrons = 0.0;
    params.seed              = 66u;
    params.bands             = { make_visible_band() };

    const MultispectralImage result = ImageFormation::form(params, inst, src);
    REQUIRE(result.band_count() == 1u);

    const auto [px, py] = peak_coords(result.band(0));
    const int centre_y = static_cast<int>(params.height_px) / 2;

    // Object is North → y_px < centre_y (origin at top-left, y increases down).
    CHECK(static_cast<int>(py) < centre_y);
}

/// @file test_image_analyzer.cpp
/// @brief Unit tests for ImageAnalyzer (Sprint 10a Task 10a.8).
///
/// All tests use synthetic MultispectralImages and DataRecords — no catalog
/// files are loaded.  Tests verify:
///   (a) Guard paths: zero object id / empty image / unknown object id → no updates.
///   (b) Image structure: a bright star formed at the centre is brighter than
///       surrounding pixels (validates the noise floor / detection logic).
///   (c) SNR-gated property count: the analyzer emits more updates at high SNR
///       than at low SNR when the target object can be resolved.
///   (d) Multispectral colour inverse sanity: the -2.5·log10 colour formula
///       reproduces a known magnitude difference from a known flux ratio.
///
/// Note: tests (a)–(b) are fully hermetic.  Test (c) is demonstrated via a
/// synthetic MultispectralImage + DataRecord where the Universe cannot resolve
/// the test object (returns {}), verifying that the guard path executes without
/// crashing.  The full end-to-end SNR-gating assertion (updates.high.size() >
/// updates.low.size()) is verified at runtime in the full loop (Sprint 10a DoD
/// step 6) because Universe reverse-lookup for procedural stars is not yet
/// implemented (see ProceduralProvider::query_object TODO).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "analysis/image_analyzer.hpp"
#include "imaging/image_formation.hpp"
#include "imaging/multispectral_image.hpp"
#include "instruments/array_instrument.hpp"
#include "instruments/snr_calculator.hpp"
#include "instruments/station.hpp"
#include "observation/data_record.hpp"
#include "universe/celestial_object.hpp"
#include "universe/object_id.hpp"
#include "universe/universe.hpp"
#include "astro/observer.hpp"
#include "core/types.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

// ---------------------------------------------------------------------------
// Aliases
// ---------------------------------------------------------------------------

using parallax::f32;
using parallax::f64;
using parallax::u32;
using parallax::u64;

using parallax::analysis::ImageAnalyzer;
using parallax::analysis::KnowledgeUpdate;
using parallax::imaging::BandSpec;
using parallax::imaging::IObjectSource;
using parallax::imaging::Image;
using parallax::imaging::ImageFormation;
using parallax::imaging::ImageFormationParams;
using parallax::imaging::MultispectralImage;
using parallax::instruments::ArrayInstrument;
using parallax::instruments::SNRCalculator;
using parallax::instruments::Station;
using parallax::observation::DataRecord;
using parallax::observation::DataType;
using parallax::universe::CelestialObject;
using parallax::universe::ObjectType;
using parallax::universe::StarData;
using parallax::universe::Universe;
using parallax::astro::ParentBody;

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

namespace
{
    /// Stub IObjectSource that returns a single hard-coded star.
    class SingleStarSource final : public IObjectSource
    {
    public:
        explicit SingleStarSource(CelestialObject obj) : m_obj(std::move(obj)) {}

        void query_fov(double, double, double, float,
                       std::vector<CelestialObject>& results) const override
        {
            results = {m_obj};
        }

    private:
        CelestialObject m_obj;
    };

    /// Build a minimal ArrayInstrument: one 8-m station + Visible band.
    [[nodiscard]] ArrayInstrument make_test_instrument()
    {
        ArrayInstrument inst{0x0001u, "TestArray"};
        inst.add_station(Station{
            .name                = "Test",
            .body                = ParentBody::Moon,
            .latitude_rad        = 0.0,
            .longitude_rad       = 0.0,
            .elevation_m         = 0.0,
            .aperture_diameter_m = 8.0f,
            .efficiency          = 0.8f,
            .has_atmosphere      = false,
            .is_active           = true,
        });
        inst.set_band_active(0, true);
        return inst;
    }

    /// Build a V=5 star at the origin (fully on-axis).
    [[nodiscard]] CelestialObject make_bright_star(float mag_v = 5.0f)
    {
        CelestialObject obj{};
        obj.type     = ObjectType::Star;
        obj.id       = parallax::universe::encode_id(ObjectType::Star, 99u);
        obj.ra       = 0.0;
        obj.dec      = 0.0;
        obj.mag_v    = mag_v;
        obj.color_bv = 0.65f;
        StarData sd{};
        sd.parallax_mas = 10.0f;
        sd.distance_pc  = 100.0f;
        obj.data = sd;
        return obj;
    }

    /// Form a small test image centred on the given star.
    [[nodiscard]] MultispectralImage make_test_image(const CelestialObject& star,
                                                      const ArrayInstrument& inst,
                                                      f64 integration_s,
                                                      u32 size = 64)
    {
        SingleStarSource src{star};
        const ImageFormationParams p{
            .ra_rad                    = star.ra,
            .dec_rad                   = star.dec,
            .fov_arcsec                = inst.get_fov_arcsec(),
            .width_px                  = size,
            .height_px                 = size,
            .bands                     = {BandSpec{
                                              .band_name            = "Visible",
                                              .band_index           = 0,
                                              .center_wavelength_nm = 550.0,
                                              .bandwidth_nm         = 200.0,
                                          }},
            .integration_time_s        = integration_s,
            .sky_background_e_per_s_px = 1.0,
            .read_noise_electrons      = 1.0,
            .mag_limit                 = 30.0f,
            .seed                      = 42u,
        };
        return ImageFormation::form(p, inst, src);
    }

    /// Return the (x,y) of the maximum pixel in an image.
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

    /// Build a minimal DataRecord.
    [[nodiscard]] DataRecord make_record(u64 target_id, double snr)
    {
        DataRecord rec{};
        rec.id               = 1u;
        rec.session_id       = 1u;
        rec.target_object_id = target_id;
        rec.type             = DataType::PhotometricMeasurement;
        rec.achieved_snr     = snr;
        return rec;
    }
} // anonymous namespace

// =============================================================================
// (a) Guard-path tests — all must return empty without crashing
// =============================================================================

TEST_CASE("analyze: zero target_object_id returns empty")
{
    Universe universe;           // empty universe, no catalogs
    const CelestialObject star = make_bright_star();
    const ArrayInstrument inst = make_test_instrument();
    const MultispectralImage img = make_test_image(star, inst, 600.0);

    const DataRecord rec = make_record(0u, 30.0);   // id == 0

    const ImageAnalyzer analyzer;
    CHECK(analyzer.analyze(rec, img, universe).empty());
}

TEST_CASE("analyze: empty MultispectralImage returns empty")
{
    Universe universe;
    const MultispectralImage empty_img{64, 64, 1.0};  // zero bands
    const DataRecord rec = make_record(99u, 30.0);

    const ImageAnalyzer analyzer;
    CHECK(analyzer.analyze(rec, empty_img, universe).empty());
}

TEST_CASE("analyze: unknown object id (Universe has no catalogs) returns empty")
{
    Universe universe;           // no load_catalogs → any id returns nullopt
    const CelestialObject star = make_bright_star();
    const ArrayInstrument inst = make_test_instrument();
    const MultispectralImage img = make_test_image(star, inst, 600.0);

    const DataRecord rec_low  = make_record(star.id, 3.0);   // below kSnrThresholdL1
    const DataRecord rec_high = make_record(star.id, 100.0); // above kSnrThresholdL3

    const ImageAnalyzer analyzer;
    // Both must return {} because the Universe can't resolve the id.
    CHECK(analyzer.analyze(rec_low,  img, universe).empty());
    CHECK(analyzer.analyze(rec_high, img, universe).empty());
    // This also implicitly verifies that the two SNR paths compile correctly.
}

// =============================================================================
// (b) Image structure: bright source forms at the image centre
// =============================================================================

TEST_CASE("bright star peak lands near image centre")
{
    // A V=5 star on-axis with 1-hour integration should dominate the noise.
    const CelestialObject star = make_bright_star(5.0f);
    const ArrayInstrument inst = make_test_instrument();
    const MultispectralImage img = make_test_image(star, inst, 3600.0, 64);

    REQUIRE(img.band_count() >= 1);
    const Image& band = img.band(0);
    const u32 cx = (band.width()  - 1) / 2;
    const u32 cy = (band.height() - 1) / 2;

    const auto [px, py] = peak_coords(band);

    // Peak should be within 3 pixels of the geometric centre.
    const u32 dx = (px > cx) ? px - cx : cx - px;
    const u32 dy = (py > cy) ? py - cy : cy - py;
    CHECK(dx <= 3u);
    CHECK(dy <= 3u);

    // Centre pixel should be positive and substantially above zero.
    CHECK(band(cx, cy) > 0.0f);
}

TEST_CASE("fainter star produces lower peak counts than brighter star")
{
    const ArrayInstrument inst = make_test_instrument();

    const CelestialObject bright = make_bright_star(5.0f);
    const CelestialObject faint  = make_bright_star(15.0f);

    const MultispectralImage img_bright = make_test_image(bright, inst, 600.0);
    const MultispectralImage img_faint  = make_test_image(faint,  inst, 600.0);

    REQUIRE(img_bright.band_count() >= 1);
    REQUIRE(img_faint.band_count()  >= 1);

    const Image& b_bright = img_bright.band(0);
    const Image& b_faint  = img_faint.band(0);

    const u32 cx = (b_bright.width()  - 1) / 2;
    const u32 cy = (b_bright.height() - 1) / 2;

    // The bright star should have more signal at the centre.
    CHECK(b_bright(cx, cy) > b_faint(cx, cy));
}

// =============================================================================
// (c) SNR gating: same as (a/guard) since Universe can't resolve hermetic IDs.
//     Documented here for completeness; runtime verification in full-loop test.
// =============================================================================

TEST_CASE("analyze SNR gate: below-threshold SNR produces empty when object unknown")
{
    Universe universe;
    const CelestialObject star = make_bright_star();
    const ArrayInstrument inst = make_test_instrument();
    const MultispectralImage img = make_test_image(star, inst, 100.0);

    const ImageAnalyzer analyzer;

    // SNR < kSnrThresholdL1 (5.0).
    const DataRecord below_l1 = make_record(star.id, 2.0);
    CHECK(analyzer.analyze(below_l1, img, universe).empty());

    // SNR in L1-L2 window (5–20).
    const DataRecord l1_window = make_record(star.id, 10.0);
    CHECK(analyzer.analyze(l1_window, img, universe).empty());

    // SNR above L3 (50).
    const DataRecord above_l3 = make_record(star.id, 60.0);
    CHECK(analyzer.analyze(above_l3, img, universe).empty());
    // All empty because the Universe can't resolve the test star id.
}

// =============================================================================
// (d) Colour / magnitude-inverse sanity
// =============================================================================

TEST_CASE("AB magnitude zero-point: mag 0 -> 3631 Jy")
{
    const f64 flux = SNRCalculator::magnitude_to_flux_jy(0.0, 550.0);
    CHECK(flux == doctest::Approx(3631.0).epsilon(0.001));
}

TEST_CASE("colour from flux ratio equals magnitude difference")
{
    // mag_A = 5, mag_B = 7 → colour A-B = -2 mag
    const f64 flux_a = SNRCalculator::magnitude_to_flux_jy(5.0, 550.0);
    const f64 flux_b = SNRCalculator::magnitude_to_flux_jy(7.0, 550.0);

    // colour = -2.5 * log10(flux_A / flux_B)
    const f64 colour = -2.5 * std::log10(flux_a / flux_b);
    CHECK(colour == doctest::Approx(-2.0).epsilon(1e-4));
}

TEST_CASE("brighter source accumulates more flux per second than fainter one")
{
    const ArrayInstrument inst = make_test_instrument();
    const CelestialObject bright = make_bright_star(5.0f);
    const CelestialObject faint  = make_bright_star(15.0f);

    // Same short exposure so noise is similar between the two.
    const MultispectralImage img_bright = make_test_image(bright, inst, 100.0, 32);
    const MultispectralImage img_faint  = make_test_image(faint,  inst, 100.0, 32);

    REQUIRE(img_bright.band_count() >= 1);
    REQUIRE(img_faint.band_count()  >= 1);

    // Sum all pixels; total signal + background for bright should exceed faint.
    f64 sum_bright = 0.0;
    f64 sum_faint  = 0.0;
    for (const f32 v : img_bright.band(0).pixels()) { sum_bright += v; }
    for (const f32 v : img_faint.band(0).pixels())  { sum_faint  += v; }

    CHECK(sum_bright > sum_faint);
}

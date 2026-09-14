/// @file test_fixture_images.cpp
/// @brief Oracle gate level 3 and physical invariants (SPECIFICA_10b §6, levels 3 and 5).
///        Also the first dedicated test of Task 10b.3 (gridding and dirty images).
///
/// Level 3: for every fixture the visibilities are rebuilt from seeds and parameters (as in level 2),
/// gridded by make_images with the fixture weighting, and the dirty beam and dirty image are compared
/// pixel by pixel with `dirtyBeam` / `dirtyImage`: |Δ| ≤ 1e-6 × peak of the reference matrix
/// (SPECIFICA §7). The battery stores both matrices rounded to 6 significant digits; each pixel
/// outside the tolerance is also classified as consistent or not with that rounding — as a diagnostic
/// only, the check itself is not relaxed.
///
/// Level 5 (independent of the oracle, so it survives any battery change): the dirty image is real
/// (hermitian gridding) and a zero baseline samples the total flux.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "glasswing_fixture_pipeline.hpp"
#include "interferometry/imaging.hpp"
#include "interferometry/uv_sampling.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <string>
#include <vector>

namespace
{
    using nlohmann::json;
    namespace fx = parallax::test_fixtures;
    namespace itf = parallax::interferometry;

    /// dirtyImage / dirtyBeam contract, SPECIFICA §7: absolute, per pixel, relative to the peak.
    constexpr double kImageAbsOfPeakTol = 1.0e-6;

    /// Hermiticity: the imaginary part is pure FFT round-off (~1e-13 of the peak).
    constexpr double kImagOfPeakTol = 1.0e-9;

    /// Zero-baseline sample and FFT centre against the summed sky flux.
    constexpr double kFluxRelTol = 1.0e-12;

    [[nodiscard]] itf::Weighting weighting_from(const json& fixture)
    {
        const std::string name = fixture.at("weighting").get<std::string>();
        REQUIRE_MESSAGE((name == "nat" || name == "uni"), "unknown weighting: " << name);
        return (name == "uni") ? itf::Weighting::Uniform : itf::Weighting::Natural;
    }

    [[nodiscard]] itf::DirtyImages images_for(const json& fixture, const fx::FixtureRun& run)
    {
        // Oracle compute(): du = 1 / thetaFov, makeImages(pts, du) on the normative grid.
        return itf::make_images(run.visibilities, 1.0 / run.theta_fov_rad, fx::kGridN, weighting_from(fixture));
    }

    /// True when `computed`, exported like the oracle does (+v.toPrecision(6)), gives `exported`.
    [[nodiscard]] bool rounds_to_export(double computed, double exported)
    {
        return std::stod(std::format("{:.5e}", computed)) == exported;
    }

    struct MatrixDivergence
    {
        double peak = 0.0;
        double max_abs = 0.0;
        std::size_t worst_pixel = 0;
        std::size_t failing = 0;
        std::size_t failing_within_export_rounding = 0;
    };

    [[nodiscard]] MatrixDivergence compare_matrix(const json& expected, const std::vector<double>& computed)
    {
        MatrixDivergence d;
        for (const json& value : expected)
        {
            d.peak = std::max(d.peak, std::abs(value.get<double>()));
        }

        const double tolerance = kImageAbsOfPeakTol * d.peak;
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            const double reference = expected[i].get<double>();
            const double diff = std::isfinite(computed[i])
                ? std::abs(computed[i] - reference)
                : std::numeric_limits<double>::infinity();
            if (diff > d.max_abs)
            {
                d.max_abs = diff;
                d.worst_pixel = i;
            }
            if (diff > tolerance)
            {
                ++d.failing;
                if (rounds_to_export(computed[i], reference))
                {
                    ++d.failing_within_export_rounding;
                }
            }
        }
        return d;
    }

    void check_matrix(const char* name, const MatrixDivergence& d)
    {
        CHECK_MESSAGE(d.failing == 0u,
            name << ": " << d.failing << " pixels beyond 1e-6 x peak (" << d.failing_within_export_rounding
            << " of them round to the exported 6-digit value); worst |d|/peak " << d.max_abs / d.peak
            << " at (x " << d.worst_pixel % fx::kGridN << ", y " << d.worst_pixel / fx::kGridN << ")");
    }

    [[nodiscard]] double max_abs(const std::vector<double>& values)
    {
        double m = 0.0;
        for (double v : values)
        {
            m = std::max(m, std::abs(v));
        }
        return m;
    }
}

TEST_CASE("Level 3: dirty beam and dirty image match every fixture")
{
    const json& all = fx::fixtures();

    for (std::size_t f = 0; f < all.size(); ++f)
    {
        const json& fixture = all.at(f);
        const std::string scenario = fixture.at("scenario").get<std::string>();
        CAPTURE(f);
        INFO("scenario: " << scenario);

        const fx::FixtureRun run = fx::run_fixture(fixture);
        const itf::DirtyImages images = images_for(fixture, run);

        const json& beam_ref = fixture.at("dirtyBeam");
        const json& image_ref = fixture.at("dirtyImage");
        const std::size_t pixels = static_cast<std::size_t>(fx::kGridN) * fx::kGridN;
        const bool shape_ok = beam_ref.size() == pixels && image_ref.size() == pixels
            && images.beam.size() == pixels && images.dirty.size() == pixels;
        CHECK_MESSAGE(shape_ok, "matrix sizes differ from N*N");
        if (!shape_ok)
        {
            continue;
        }

        const MatrixDivergence beam = compare_matrix(beam_ref, images.beam);
        const MatrixDivergence dirty = compare_matrix(image_ref, images.dirty);

        MESSAGE("fixture " << f << " " << scenario << " (" << fixture.at("weighting").get<std::string>() << ")"
                << " | beam max|d|/peak " << beam.max_abs / beam.peak << ", fail " << beam.failing
                << " (within export rounding " << beam.failing_within_export_rounding << ")"
                << " | image max|d|/peak " << dirty.max_abs / dirty.peak << ", fail " << dirty.failing
                << " (within export rounding " << dirty.failing_within_export_rounding << ")");

        check_matrix("dirtyBeam", beam);
        check_matrix("dirtyImage", dirty);
    }
}

TEST_CASE("Level 5: the dirty image is real (hermitian gridding)")
{
    const json& all = fx::fixtures();

    for (std::size_t f = 0; f < all.size(); ++f)
    {
        const json& fixture = all.at(f);
        CAPTURE(f);
        INFO("scenario: " << fixture.at("scenario").get<std::string>());

        const fx::FixtureRun run = fx::run_fixture(fixture);
        const itf::DirtyImages images = images_for(fixture, run);
        REQUIRE(images.dirty_imag.size() == images.dirty.size());

        const double peak = max_abs(images.dirty);
        const double imag = max_abs(images.dirty_imag);
        REQUIRE(peak > 0.0);
        MESSAGE("fixture " << f << ": max |Im| / peak of the dirty image " << imag / peak);
        CHECK_MESSAGE(imag <= kImagOfPeakTol * peak, "max |Im| / peak = " << imag / peak);
    }
}

TEST_CASE("Level 5: a zero baseline samples the total flux")
{
    const json& all = fx::fixtures();

    for (std::size_t f = 0; f < all.size(); ++f)
    {
        const json& fixture = all.at(f);
        CAPTURE(f);
        INFO("scenario: " << fixture.at("scenario").get<std::string>());

        const fx::FixtureRun run = fx::run_fixture(fixture);
        const itf::TargetFT& ft = run.target_ft;
        const double flux = ft.flux_total;
        REQUIRE(flux > 0.0);

        // Centre of the centred spectrum = DC = sum of the sky.
        const std::size_t centre = static_cast<std::size_t>(ft.N / 2u) * ft.N + ft.N / 2u;
        CHECK_MESSAGE(std::abs(ft.Fre[centre] - flux) <= kFluxRelTol * flux,
            "Re F(0,0) = " << ft.Fre[centre] << ", flux " << flux);
        CHECK_MESSAGE(std::abs(ft.Fim[centre]) <= kFluxRelTol * flux, "Im F(0,0) = " << ft.Fim[centre]);

        // Same point through sample_uv: two co-located stations have B = 0, hence u = v = 0.
        const itf::Station station = fx::oracle_stations(fixture).front();
        const itf::ObservationConfig observation {
            .dec_rad = fx::oracle_deg_to_rad(fixture.at("decDeg").get<double>()),
            .lambda_m = fixture.at("lambdaMeters").get<double>(),
            .duration_hours = 0.0,
            .rotation = false,
            .mode = itf::InstrumentMode::Radio,
            .theta_fov_rad = run.theta_fov_rad,
            .flux_total = flux,
        };
        const std::vector<itf::Visibility> zero =
            itf::sample_uv({station, station}, observation, ft, itf::StationErrors {});

        CHECK_MESSAGE(zero.size() == 1u, "the station must be visible at t = 0 for this declination");
        for (const itf::Visibility& sample : zero)
        {
            MESSAGE("fixture " << f << ": |Re F(0,0) - flux| / flux " << std::abs(ft.Fre[centre] - flux) / flux
                    << ", |tVr(0,0) - flux| / flux " << std::abs(sample.tVr - flux) / flux
                    << ", |tVi(0,0)| / flux " << std::abs(sample.tVi) / flux);
            CHECK(sample.u == 0.0);
            CHECK(sample.v == 0.0);
            CHECK_MESSAGE(std::abs(sample.tVr - flux) <= kFluxRelTol * flux, "tVr(0,0) = " << sample.tVr);
            CHECK_MESSAGE(std::abs(sample.tVi) <= kFluxRelTol * flux, "tVi(0,0) = " << sample.tVi);
        }
    }
}

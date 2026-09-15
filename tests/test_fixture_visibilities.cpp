/// @file test_fixture_visibilities.cpp
/// @brief Oracle gate, level 2 (SPECIFICA_10b §6): (u,v) and visibilities against the fixture battery.
///
/// For every fixture the whole chain is rebuilt from seeds and parameters only:
/// target model (seed, requested class, complexity) → sky at the fixture band and epoch → centred FFT
/// → stations (mode, site) → sample_uv with the fixture's error model (atmSeed, turbulence, gains, SNR).
/// The emitted samples are compared, in order, with the fixture's `visibilities`.
///
/// Relative errors are taken per field and normalised by the reference modulus of the pair the field
/// belongs to: |Δu| and |Δv| by |(u,v)|, |ΔVr| and |ΔVi| by |V|, |ΔtVr| and |ΔtVi| by |tV|. A component
/// close to zero (e.g. tVi of a symmetric source) is therefore held to the same bound as its modulus.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "glasswing_fixture_pipeline.hpp"
#include "interferometry/uv_sampling.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace
{
    using nlohmann::json;
    namespace fx = parallax::test_fixtures;
    namespace itf = parallax::interferometry;

    /// (u,v) contract, SPECIFICA §7.
    constexpr double kUvRelTol = 1.0e-9;

    /// True and corrupted visibility contract, SPECIFICA §7.
    constexpr double kVisibilityRelTol = 1.0e-7;

    constexpr std::size_t kNoMismatch = std::numeric_limits<std::size_t>::max();

    /// Error of one field normalised by the reference modulus of its pair. Exact matches give 0;
    /// a non-finite computed value or a zero reference modulus with a non-zero error gives +inf.
    [[nodiscard]] double field_error(double computed, double reference, double reference_modulus)
    {
        if (!std::isfinite(computed))
        {
            return std::numeric_limits<double>::infinity();
        }
        const double absolute = std::abs(computed - reference);
        if (absolute == 0.0)
        {
            return 0.0;
        }
        return reference_modulus > 0.0 ? absolute / reference_modulus : std::numeric_limits<double>::infinity();
    }

    struct FieldWorst
    {
        double error = 0.0;
        std::size_t sample = 0;

        void track(double candidate, std::size_t index)
        {
            if (candidate > error)
            {
                error = candidate;
                sample = index;
            }
        }
    };

    struct VisibilityDivergence
    {
        FieldWorst u;
        FieldWorst v;
        FieldWorst vr;
        FieldWorst vi;
        FieldWorst tvr;
        FieldWorst tvi;
        std::size_t first_k_mismatch = kNoMismatch;
    };

    [[nodiscard]] VisibilityDivergence compare_visibilities(
        const json& expected,
        const std::vector<itf::Visibility>& computed)
    {
        VisibilityDivergence d;
        const std::size_t count = std::min(expected.size(), computed.size());

        for (std::size_t i = 0; i < count; ++i)
        {
            const json& e = expected.at(i);
            const itf::Visibility& c = computed[i];

            if (d.first_k_mismatch == kNoMismatch && e.at("k").get<std::uint32_t>() != c.time_index)
            {
                d.first_k_mismatch = i;
            }

            const double u = e.at("u").get<double>();
            const double v = e.at("v").get<double>();
            const double uv_modulus = std::hypot(u, v);
            d.u.track(field_error(c.u, u, uv_modulus), i);
            d.v.track(field_error(c.v, v, uv_modulus), i);

            const double vr = e.at("Vr").get<double>();
            const double vi = e.at("Vi").get<double>();
            const double v_modulus = std::hypot(vr, vi);
            d.vr.track(field_error(c.Vr, vr, v_modulus), i);
            d.vi.track(field_error(c.Vi, vi, v_modulus), i);

            const double tvr = e.at("trueVr").get<double>();
            const double tvi = e.at("trueVi").get<double>();
            const double tv_modulus = std::hypot(tvr, tvi);
            d.tvr.track(field_error(c.tVr, tvr, tv_modulus), i);
            d.tvi.track(field_error(c.tVi, tvi, tv_modulus), i);
        }

        return d;
    }

    void check_field(const std::string& name, const FieldWorst& worst, double tolerance)
    {
        CHECK_MESSAGE(worst.error <= tolerance,
            name << ": worst relative error " << worst.error << " at sample " << worst.sample
            << " (tolerance " << tolerance << ")");
    }

    /// Error models in which the station-error stream can be compared without trusting the
    /// target model: a pure phase screen (V = e^{iΔφ}·tV) or pure thermal noise (V = tV + F/SNR·z).
    enum class StreamRegime
    {
        None,
        PhaseOnly,
        NoiseOnly,
        NotSeparable,
    };

    [[nodiscard]] StreamRegime stream_regime(const json& fixture)
    {
        const bool turbulence = fixture.at("turbulenceRms").get<double>() > 0.0;
        const bool noise = fixture.at("snr").get<double>() > 0.0;
        const bool gains = fixture.at("gainErrors").get<bool>();

        if (!turbulence && !noise && !gains)
        {
            return StreamRegime::None;
        }
        if (turbulence && !noise && !gains)
        {
            return StreamRegime::PhaseOnly;
        }
        if (noise && !turbulence && !gains)
        {
            return StreamRegime::NoiseOnly;
        }
        return StreamRegime::NotSeparable;
    }
}

TEST_CASE("Level 2: (u,v) and visibilities match every fixture")
{
    const json& all = fx::fixtures();

    for (std::size_t f = 0; f < all.size(); ++f)
    {
        const json& fixture = all.at(f);
        const std::string scenario = fixture.at("scenario").get<std::string>();
        CAPTURE(f);
        INFO("scenario: " << scenario);

        CHECK(fixture.at("gridN").get<std::uint32_t>() == fx::kGridN);

        const fx::FixtureRun run = fx::run_fixture(fixture);
        const json& expected = fixture.at("visibilities");

        // Target-model prerequisites: a divergence here explains any visibility failure below.
        CHECK(run.model.subtype == fixture.at("subtype").get<std::string>());

        const VisibilityDivergence d = compare_visibilities(expected, run.visibilities);

        MESSAGE("fixture " << f << " " << scenario << ": samples C++ " << run.visibilities.size()
                << " / oracle " << expected.size()
                << " | thetaFov rel " << fx::relative_error(run.theta_fov_rad, fixture.at("thetaFovRad").get<double>())
                << ", flux rel " << fx::relative_error(run.target_ft.flux_total, fixture.at("fluxTotal").get<double>())
                << " | u " << d.u.error << ", v " << d.v.error
                << ", Vr " << d.vr.error << ", Vi " << d.vi.error
                << ", tVr " << d.tvr.error << ", tVi " << d.tvi.error);

        CHECK_MESSAGE(run.visibilities.size() == expected.size(),
            "sample count: C++ " << run.visibilities.size() << ", oracle " << expected.size());
        CHECK_MESSAGE(d.first_k_mismatch == kNoMismatch,
            "first sample with a different time index: " << d.first_k_mismatch);

        check_field("u", d.u, kUvRelTol);
        check_field("v", d.v, kUvRelTol);
        check_field("Vr", d.vr, kVisibilityRelTol);
        check_field("Vi", d.vi, kVisibilityRelTol);
        check_field("tVr", d.tvr, kVisibilityRelTol);
        check_field("tVi", d.tvi, kVisibilityRelTol);
    }
}

TEST_CASE("Level 2, target-independent: station-error stream matches where it is separable")
{
    // Compares only what the error stream contributes, so it holds even if the sky differs:
    //   phase only: V / tV = e^{i(φi − φj)}            (Kolmogorov draws, pair order)
    //   noise only: (V − tV) / flux = z / SNR           (noise draws, time/pair loop order)
    using Complex = std::complex<double>;
    const json& all = fx::fixtures();

    for (std::size_t f = 0; f < all.size(); ++f)
    {
        const json& fixture = all.at(f);
        const StreamRegime regime = stream_regime(fixture);
        if (regime == StreamRegime::None)
        {
            continue;
        }

        const std::string scenario = fixture.at("scenario").get<std::string>();
        CAPTURE(f);
        INFO("scenario: " << scenario);

        if (regime == StreamRegime::NotSeparable)
        {
            MESSAGE("fixture " << f << " " << scenario
                    << ": several error terms at once, not separable from the target model");
            continue;
        }

        const fx::FixtureRun run = fx::run_fixture(fixture);
        const json& expected = fixture.at("visibilities");
        CHECK(run.visibilities.size() == expected.size());
        if (run.visibilities.size() != expected.size())
        {
            continue;
        }

        const double flux_oracle = fixture.at("fluxTotal").get<double>();
        FieldWorst worst;
        std::size_t skipped = 0;

        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            const json& e = expected.at(i);
            const itf::Visibility& c = run.visibilities[i];
            const Complex v_oracle {e.at("Vr").get<double>(), e.at("Vi").get<double>()};
            const Complex tv_oracle {e.at("trueVr").get<double>(), e.at("trueVi").get<double>()};
            const Complex v_cpp {c.Vr, c.Vi};
            const Complex tv_cpp {c.tVr, c.tVi};

            Complex q_oracle;
            Complex q_cpp;
            if (regime == StreamRegime::PhaseOnly)
            {
                if (tv_oracle == 0.0 || tv_cpp == 0.0)
                {
                    ++skipped;
                    continue;
                }
                q_oracle = v_oracle / tv_oracle;
                q_cpp = v_cpp / tv_cpp;
            }
            else
            {
                q_oracle = (v_oracle - tv_oracle) / flux_oracle;
                q_cpp = (v_cpp - tv_cpp) / run.target_ft.flux_total;
            }

            const double absolute = std::abs(q_cpp - q_oracle);
            const double modulus = std::abs(q_oracle);
            const double relative = (absolute == 0.0)
                ? 0.0
                : (modulus > 0.0 ? absolute / modulus : std::numeric_limits<double>::infinity());
            worst.track(std::isfinite(relative) ? relative : std::numeric_limits<double>::infinity(), i);
        }

        const std::string what = (regime == StreamRegime::PhaseOnly) ? "V/tV" : "(V-tV)/flux";
        MESSAGE("fixture " << f << " " << scenario << ": " << what << " worst rel " << worst.error
                << " at sample " << worst.sample << " (" << skipped << " samples skipped)");
        check_field(what, worst, kVisibilityRelTol);
    }
}

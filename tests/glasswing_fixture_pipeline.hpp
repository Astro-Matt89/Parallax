#pragma once

/// @file glasswing_fixture_pipeline.hpp
/// @brief Rebuilds the oracle pipeline for one fixture from seeds and parameters only:
///        target model → sky at the fixture band and epoch → centred FFT → stations → sample_uv.
///        Shared by the level-2 and level-3 oracle gates (SPECIFICA_10b §6).
///
/// The including target must also compile the procedural, core/fft and interferometry sources.

#include "glasswing_fixture_battery.hpp"
#include "interferometry/uv_sampling.hpp"
#include "procedural/target_families.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace parallax::test_fixtures
{
    /// Normative sky grid of the battery.
    inline constexpr std::uint32_t kGridN = 128;

    [[nodiscard]] inline procedural::Complexity complexity_from(const nlohmann::json& fixture)
    {
        // Oracle COMPLEXITY_CAP keys; any other value draws the cap from capRoll ("free").
        const std::string name = fixture.at("complexity").get<std::string>();
        if (name == "simple")
        {
            return procedural::Complexity::Simple;
        }
        if (name == "structured")
        {
            return procedural::Complexity::Structured;
        }
        if (name == "complex")
        {
            return procedural::Complexity::Complex;
        }
        return procedural::Complexity::Free;
    }

    struct FixtureRun
    {
        procedural::TargetModel model;
        interferometry::TargetFT target_ft;
        double theta_fov_rad = 0.0;
        std::vector<interferometry::Visibility> visibilities;
    };

    /// Oracle regenTarget() + refreshTargetRender() + compute(), from fixture parameters only.
    [[nodiscard]] inline FixtureRun run_fixture(const nlohmann::json& fixture)
    {
        procedural::TargetOptions options;
        const int requested_class = fixture.at("requestedClass").get<int>();
        if (requested_class >= 0)
        {
            options.forced_family = static_cast<procedural::Family>(static_cast<std::uint8_t>(requested_class));
        }
        options.complexity = complexity_from(fixture);

        const double lambda_m = fixture.at("lambdaMeters").get<double>();

        FixtureRun run;
        run.model = procedural::generate_target_model(fixture.at("seed").get<std::uint32_t>(), options);
        // Oracle genTarget: thetaFov = thetaObj * fovMul.
        run.theta_fov_rad = run.model.theta_obj * run.model.fov_mul;

        const std::vector<double> sky = procedural::render_target_at(
            run.model, lambda_m, fixture.at("epochDays").get<double>(), kGridN);
        run.target_ft = procedural::compute_target_fft(sky, kGridN);

        const interferometry::ObservationConfig observation {
            .dec_rad = oracle_deg_to_rad(fixture.at("decDeg").get<double>()),
            .lambda_m = lambda_m,
            .duration_hours = fixture.at("durationHours").get<double>(),
            .rotation = fixture.at("rotation").get<bool>(),
            .mode = instrument_mode(fixture),
            .theta_fov_rad = run.theta_fov_rad,
            .flux_total = run.target_ft.flux_total,
        };
        const interferometry::StationErrors errors {
            .turbulence_rms_rad = fixture.at("turbulenceRms").get<double>(),
            .snr = fixture.at("snr").get<double>(),
            .gain_errors = fixture.at("gainErrors").get<bool>(),
            .atm_seed = fixture.at("atmSeed").get<std::uint32_t>(),
        };

        run.visibilities = interferometry::sample_uv(oracle_stations(fixture), observation, run.target_ft, errors);
        return run;
    }
}

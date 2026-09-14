/// @file dump_cpp_target.cpp
/// @brief Diagnostic tool: dump the C++ target model and rendered sky for one fixture of the Glasswing
///        battery, using the oracle's JSON field names, so diff_targets.py can compare it with
///        dump_oracle_target.js.
///
/// Usage: dump_cpp_target <fixture-index> <out.json>
///        dump_cpp_target --models <cases.json> <out.json>   (models only, cases from sweep_models.js)

#include "procedural/target_families.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    using nlohmann::json;
    namespace proc = parallax::procedural;

    constexpr const char* kBatteryPath = PLX_FIXTURE_BATTERY_PATH;

    [[nodiscard]] std::string primitive_name(proc::PrimitiveType type)
    {
        switch (type)
        {
            case proc::PrimitiveType::Point: return "point";
            case proc::PrimitiveType::Gaussian: return "gaussian";
            case proc::PrimitiveType::Disk: return "disk";
            case proc::PrimitiveType::Ring: return "ring";
            case proc::PrimitiveType::Jet: return "jet";
            case proc::PrimitiveType::PlanetSurface: return "planet_surface";
            case proc::PrimitiveType::Absorption: return "absorption";
        }
        return "?";
    }

    [[nodiscard]] std::string spectral_name(proc::SpectralModel model)
    {
        switch (model)
        {
            case proc::SpectralModel::Stellar: return "stellar";
            case proc::SpectralModel::ThermalDust: return "thermal_dust";
            case proc::SpectralModel::Synchrotron: return "synchrotron";
            case proc::SpectralModel::FreeFree: return "free_free";
            case proc::SpectralModel::Maser: return "maser";
        }
        return "?";
    }

    [[nodiscard]] std::string temporal_name(proc::TemporalModel model)
    {
        switch (model)
        {
            case proc::TemporalModel::Static: return "static";
            case proc::TemporalModel::Orbit: return "orbit";
            case proc::TemporalModel::Rotation: return "rotation";
            case proc::TemporalModel::Expansion: return "expansion";
            case proc::TemporalModel::ProperMotion: return "proper_motion";
            case proc::TemporalModel::MultiOrbit: return "multi_orbit";
            case proc::TemporalModel::PlanetRotation: return "planet_rotation";
        }
        return "?";
    }

    [[nodiscard]] std::string rarity_name(proc::Rarity rarity)
    {
        switch (rarity)
        {
            case proc::Rarity::Common: return "COMMON";
            case proc::Rarity::ComplexTier: return "COMPLEX";
            case proc::Rarity::Uncommon: return "UNCOMMON";
            case proc::Rarity::Rare: return "RARE";
            case proc::Rarity::Exceptional: return "EXCEPTIONAL";
        }
        return "?";
    }

    [[nodiscard]] proc::Complexity complexity_from(const std::string& name)
    {
        if (name == "simple") { return proc::Complexity::Simple; }
        if (name == "structured") { return proc::Complexity::Structured; }
        if (name == "complex") { return proc::Complexity::Complex; }
        return proc::Complexity::Free;
    }

    [[nodiscard]] json shape_to_json(const proc::Component& c)
    {
        json harm = json::array();
        for (const proc::HarmonicTerm& h : c.harm)
        {
            harm.push_back({{"k", h.k}, {"A", h.A}, {"ph", h.ph}});
        }
        json knots = json::array();
        for (const proc::JetKnot& k : c.knots)
        {
            knots.push_back({{"t", k.t}, {"dAng", k.dAng}, {"amp", k.amp}, {"sig", k.sig}});
        }
        json j = {
            {"sigma", c.sigma}, {"sigmaX", c.sigma_x}, {"sigmaY", c.sigma_y}, {"angle", c.angle},
            {"radius", c.radius}, {"ellipticity", c.ellipticity}, {"limbDarkening", c.limb_darkening},
            {"width", c.width}, {"harm", harm},
            {"length", c.length}, {"curvature", c.curvature}, {"counterJetRatio", c.counter_jet_ratio},
            {"knots", knots},
            {"kind", c.kind}, {"nz", c.nz}, {"rotPhase", c.rot_phase}, {"cloudPhase", c.cloud_phase},
            {"phaseAngle", c.phase_angle}, {"seaLevel", c.sea_level}, {"capLat", c.cap_lat},
            {"cloudAmt", c.cloud_amt}, {"shape", c.shape}, {"depth", c.depth}, {"rot", c.rot},
        };
        if (c.orbit)
        {
            j["orbit"] = {
                {"aPx", c.orbit->aPx}, {"periodYears", c.orbit->periodYears}, {"phase0", c.orbit->phase0},
                {"cosI", c.orbit->cosI}, {"pa", c.orbit->pa},
            };
        }
        return j;
    }

    [[nodiscard]] json component_to_json(const proc::Component& c)
    {
        json j = {
            {"id", c.id}, {"type", primitive_name(c.type)}, {"spectralModel", spectral_name(c.spectral_model)},
            {"x", c.x}, {"y", c.y}, {"flux", c.flux}, {"fluxRef", c.flux_ref},
            {"referenceFrequencyHz", c.reference_freq_hz}, {"alpha", c.alpha}, {"sizeIndex", c.size_index},
            {"lineFrequencyHz", c.line_freq_hz}, {"maserAmp", c.maser_amp},
        };
        j.update(shape_to_json(c));
        return j;
    }

    [[nodiscard]] json physical_to_json(const proc::TargetModel::PhysicalMap& p)
    {
        return {
            {"separation_au", p.separation_au}, {"distance_pc", p.distance_pc}, {"distance_kpc", p.distance_kpc},
            {"distance_mpc", p.distance_mpc}, {"flux_ratio", p.flux_ratio}, {"total_mass_solar", p.total_mass_solar},
            {"primary_radius_solar", p.primary_radius_solar}, {"mass_ratio", p.mass_ratio},
            {"radius_solar", p.radius_solar}, {"limb_darkening", p.limb_darkening}, {"oblateness", p.oblateness},
            {"radius_jupiter", p.radius_jupiter}, {"temp_k", p.temp_k}, {"rotation_hours", p.rotation_hours},
            {"inner_disk_au", p.inner_disk_au}, {"inclination_deg", p.inclination_deg},
            {"jet_length_pc", p.jet_length_pc}, {"spiral_radius_au", p.spiral_radius_au},
            {"period_years", p.period_years}, {"shell_age_years", p.shell_age_years},
            {"disk_radius_au", p.disk_radius_au}, {"position_angle_deg", p.position_angle_deg},
            {"cavity_au", p.cavity_au}, {"ring_count", p.ring_count}, {"nebula_radius_pc", p.nebula_radius_pc},
            {"period_ms", p.period_ms}, {"dm_pc_cm3", p.dm_pc_cm3}, {"shell_radius_au", p.shell_radius_au},
            {"expansion_kms", p.expansion_kms}, {"age_years", p.age_years},
            {"core_spectral_index", p.core_spectral_index}, {"jet_spectral_index", p.jet_spectral_index},
            {"jet_length_kpc", p.jet_length_kpc}, {"knot_count", p.knot_count}, {"jet_pa_deg", p.jet_pa_deg},
            {"lobe_span_kpc", p.lobe_span_kpc}, {"lobe_asymmetry", p.lobe_asymmetry},
            {"ring_diameter_uas", p.ring_diameter_uas}, {"mass_billion_solar", p.mass_billion_solar},
            {"doppler_asymmetry", p.doppler_asymmetry}, {"star_mass_solar", p.star_mass_solar},
            {"outer_ring_au", p.outer_ring_au}, {"planet_count", p.planet_count}, {"outermost_au", p.outermost_au},
            {"radius_earth", p.radius_earth}, {"surface_type", p.surface_type},
        };
    }

    [[nodiscard]] json model_to_json(const proc::TargetModel& m)
    {
        json components = json::array();
        for (const proc::Component& c : m.components)
        {
            components.push_back(component_to_json(c));
        }
        return {
            {"seed", m.seed}, {"famIdx", m.fam_idx}, {"subtype", m.subtype}, {"rarity", rarity_name(m.rarity)},
            {"thetaObj", m.theta_obj}, {"fovMul", m.fov_mul}, {"modifiers", m.modifiers},
            {"temporal", {
                {"model", temporal_name(m.temporal.model)}, {"phase0", m.temporal.phase0},
                {"periodYears", m.temporal.period_years}, {"outerPeriodYears", m.temporal.outer_period_years},
                {"knotSpeedC", m.temporal.knot_speed_c}, {"ratePxPerYear", m.temporal.rate_px_per_year},
            }},
            {"physical", physical_to_json(m.physical)},
            {"components", components},
        };
    }

    /// generateTargetModel options from {requestedClass, complexity} (a fixture or a sweep case).
    [[nodiscard]] proc::TargetOptions options_from(const json& params)
    {
        proc::TargetOptions options;
        const int requested_class = params.at("requestedClass").get<int>();
        if (requested_class >= 0)
        {
            options.forced_family = static_cast<proc::Family>(static_cast<std::uint8_t>(requested_class));
        }
        options.complexity = complexity_from(params.at("complexity").get<std::string>());
        return options;
    }

    [[nodiscard]] json dump_models(const json& cases)
    {
        json models = json::array();
        for (const json& params : cases)
        {
            const proc::TargetModel model =
                proc::generate_target_model(params.at("seed").get<std::uint32_t>(), options_from(params));
            models.push_back(model_to_json(model));
        }
        return models;
    }

    [[nodiscard]] json dump_fixture(const json& fixture, std::size_t index)
    {
        const std::uint32_t grid_n = fixture.at("gridN").get<std::uint32_t>();
        const proc::TargetModel model =
            proc::generate_target_model(fixture.at("seed").get<std::uint32_t>(), options_from(fixture));
        const std::vector<double> sky = proc::render_target_at(
            model, fixture.at("lambdaMeters").get<double>(), fixture.at("epochDays").get<double>(), grid_n);

        double flux = 0.0;
        for (double v : sky)
        {
            flux += v;
        }

        return {
            {"fixture", index}, {"scenario", fixture.at("scenario")}, {"gridN", grid_n},
            {"model", model_to_json(model)}, {"sky", sky}, {"flux", flux},
            {"thetaFovRad", model.theta_obj * model.fov_mul},
        };
    }
}

int main(int argc, char** argv)
{
    const bool models_mode = (argc == 4 && std::string(argv[1]) == "--models");
    if (argc != 3 && !models_mode)
    {
        spdlog::error("usage: dump_cpp_target <fixture-index> <out.json> | --models <cases.json> <out.json>");
        return EXIT_FAILURE;
    }

    try
    {
        if (models_mode)
        {
            std::ifstream cases(argv[2]);
            const json models = dump_models(json::parse(cases));
            std::ofstream(argv[3]) << models.dump();
            spdlog::info("{} models written", models.size());
            return EXIT_SUCCESS;
        }

        std::ifstream in(kBatteryPath);
        const json battery = json::parse(in);
        const std::size_t index = std::stoul(argv[1]);
        const json out = dump_fixture(battery.at("fixtures").at(index), index);

        std::ofstream(argv[2]) << out.dump();
        spdlog::info("fixture {} {}: {} components, flux {}", index, out.at("scenario").get<std::string>(),
            out.at("model").at("components").size(), out.at("flux").get<double>());
        return EXIT_SUCCESS;
    }
    catch (const std::exception& ex)
    {
        spdlog::error("dump_cpp_target failed: {}", ex.what());
        return EXIT_FAILURE;
    }
}

#pragma once

/// @file glasswing_fixture_battery.hpp
/// @brief Shared helpers for the oracle gate tests (SPECIFICA_10b §6): loading the fixture battery
///        and rebuilding the oracle's inputs from fixture parameters only.
///
/// The including target must define PLX_FIXTURE_BATTERY_PATH (see tests/CMakeLists.txt).

#include "interferometry/array_config.hpp"
#include "interferometry/ephemeris.hpp"
#include "interferometry/uv_sampling.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace parallax::test_fixtures
{
    inline constexpr const char* kBatteryPath = PLX_FIXTURE_BATTERY_PATH;

    inline constexpr std::size_t kFixtureCount = 15;

    /// Antennas per arm of the sandbox "y" preset, the layout the battery was exported with.
    inline constexpr std::uint32_t kSandboxAntennasPerArm = 4;

    /// Parse the battery once; returns a discarded value if the file is missing or malformed.
    [[nodiscard]] inline const nlohmann::json& battery()
    {
        static const nlohmann::json kBattery = []
        {
            std::ifstream stream(kBatteryPath);
            return stream.is_open()
                ? nlohmann::json::parse(stream, nullptr, false)
                : nlohmann::json(nlohmann::json::value_t::discarded);
        }();
        return kBattery;
    }

    [[nodiscard]] inline const nlohmann::json& fixtures()
    {
        const nlohmann::json& root = battery();
        REQUIRE_MESSAGE(!root.is_discarded(), "cannot read fixture battery: " << kBatteryPath);
        REQUIRE(root.contains("fixtures"));
        REQUIRE(root.at("fixtures").size() == kFixtureCount);
        return root.at("fixtures");
    }

    [[nodiscard]] inline double relative_error(double computed, double reference)
    {
        return std::abs(computed - reference) / std::abs(reference);
    }

    /// Same operation order as the sandbox (deg * Math.PI / 180).
    [[nodiscard]] inline double oracle_deg_to_rad(double degrees)
    {
        return degrees * astro_constants::kPi / 180.0;
    }

    [[nodiscard]] inline interferometry::InstrumentMode instrument_mode(const nlohmann::json& fixture)
    {
        const std::string instrument = fixture.at("instrument").get<std::string>();
        if (instrument == "comb")
        {
            return interferometry::InstrumentMode::Comb;
        }
        if (instrument == "hbt")
        {
            return interferometry::InstrumentMode::Hbt;
        }
        if (instrument == "epr")
        {
            return interferometry::InstrumentMode::Epr;
        }
        REQUIRE_MESSAGE(instrument == "radio", "unknown instrument: " << instrument);
        return interferometry::InstrumentMode::Radio;
    }

    /// Oracle compute(): HBT and optical comb keep Earth stations only (coherence on the ground).
    [[nodiscard]] inline bool keeps_only_earth_stations(interferometry::InstrumentMode mode)
    {
        return mode == interferometry::InstrumentMode::Hbt || mode == interferometry::InstrumentMode::Comb;
    }

    /// Rebuild the oracle's buildStations() list from fixture parameters (before any instrument filter).
    [[nodiscard]] inline std::vector<interferometry::Station> oracle_stations(const nlohmann::json& fixture)
    {
        using interferometry::Body;
        using interferometry::SiteCenter;
        using interferometry::Station;

        const std::string mode = fixture.at("mode").get<std::string>();
        if (mode == "earth" || mode == "full")
        {
            std::vector<Station> stations = interferometry::earth_stations();
            if (mode == "full")
            {
                stations.push_back(Station {
                    .name = "Tycho",
                    .body = Body::Moon,
                    .lat = interferometry::kTychoLat,
                    .lon = interferometry::kTychoLon,
                });
            }
            return stations;
        }

        const bool on_moon = fixture.at("siteBody").get<std::string>() == "moon";

        interferometry::ArrayConfig config;
        config.geometry = interferometry::ArrayGeometry::Y;
        config.antennas_per_arm = kSandboxAntennasPerArm;
        config.site_extent_m = fixture.at("siteScaleM").get<double>();
        // The sandbox Earth site only exposes latitude: its centre longitude is always 0.
        config.site = on_moon
            ? SiteCenter {
                  .body = Body::Moon,
                  .lat = interferometry::kTychoLat,
                  .lon = interferometry::kTychoLon,
              }
            : SiteCenter {
                  .body = Body::Earth,
                  .lat = oracle_deg_to_rad(fixture.at("siteLatDeg").get<double>()),
                  .lon = 0.0,
              };
        return interferometry::generate_stations(config);
    }
}

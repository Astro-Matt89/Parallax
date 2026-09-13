/// @file test_station_positions.cpp
/// @brief Oracle gate, level 1 (SPECIFICA_10b §6): station positions against the fixture battery.
///
/// For every fixture in glasswing_fixture_battery_v1_1.json the station list is rebuilt from the
/// fixture parameters alone (mode, site body/latitude/scale, instrument) through the C++ array code,
/// propagated with station_state() at each sample time, and compared with stationPositionsPerSampleM.
/// The fixture's own latRad/lonRad descriptors are deliberately NOT used as input.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "core/types.hpp"
#include "interferometry/array_config.hpp"
#include "interferometry/ephemeris.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    using nlohmann::json;
    using parallax::Vec3d;
    using parallax::astro_constants::kPi;
    using parallax::interferometry::ArrayConfig;
    using parallax::interferometry::ArrayGeometry;
    using parallax::interferometry::Body;
    using parallax::interferometry::SiteCenter;
    using parallax::interferometry::Station;

    constexpr const char* kBatteryPath = PLX_FIXTURE_BATTERY_PATH;

    /// Relative tolerance on |P_cpp - P_oracle| / |P_oracle| (uv contract, SPECIFICA §7).
    constexpr double kPositionRelTol = 1.0e-9;

    /// Relative tolerance for the ephemeris constants exported alongside each fixture.
    constexpr double kConstantRelTol = 1.0e-12;

    /// Antennas per arm of the sandbox "y" preset, the layout the battery was exported with.
    constexpr std::uint32_t kSandboxAntennasPerArm = 4;

    /// Parse the battery once; returns a discarded value if the file is missing or malformed.
    [[nodiscard]] const json& battery()
    {
        static const json kBattery = []
        {
            std::ifstream stream(kBatteryPath);
            return stream.is_open() ? json::parse(stream, nullptr, false) : json(json::value_t::discarded);
        }();
        return kBattery;
    }

    [[nodiscard]] const json& fixtures()
    {
        const json& root = battery();
        REQUIRE_MESSAGE(!root.is_discarded(), "cannot read fixture battery: " << kBatteryPath);
        REQUIRE(root.contains("fixtures"));
        REQUIRE(root.at("fixtures").size() == 15u);
        return root.at("fixtures");
    }

    [[nodiscard]] double relative_error(double computed, double reference)
    {
        return std::abs(computed - reference) / std::abs(reference);
    }

    /// Same operation order as the sandbox (deg * Math.PI / 180).
    [[nodiscard]] double oracle_deg_to_rad(double degrees)
    {
        return degrees * kPi / 180.0;
    }

    /// Rebuild the oracle's station list (buildStations + instrument filter) from fixture parameters.
    [[nodiscard]] std::vector<Station> build_fixture_stations(const json& fixture)
    {
        const std::string mode = fixture.at("mode").get<std::string>();
        std::vector<Station> stations;

        if (mode == "earth" || mode == "full")
        {
            stations = parallax::interferometry::earth_stations();
            if (mode == "full")
            {
                stations.push_back(Station {
                    .name = "Tycho",
                    .body = Body::Moon,
                    .lat = parallax::interferometry::kTychoLat,
                    .lon = parallax::interferometry::kTychoLon,
                });
            }
        }
        else
        {
            const bool on_moon = fixture.at("siteBody").get<std::string>() == "moon";

            ArrayConfig config;
            config.geometry = ArrayGeometry::Y;
            config.antennas_per_arm = kSandboxAntennasPerArm;
            config.site_extent_m = fixture.at("siteScaleM").get<double>();
            // The sandbox Earth site only exposes latitude: its centre longitude is always 0.
            config.site = on_moon
                ? SiteCenter {
                      .body = Body::Moon,
                      .lat = parallax::interferometry::kTychoLat,
                      .lon = parallax::interferometry::kTychoLon,
                  }
                : SiteCenter {
                      .body = Body::Earth,
                      .lat = oracle_deg_to_rad(fixture.at("siteLatDeg").get<double>()),
                      .lon = 0.0,
                  };
            stations = parallax::interferometry::generate_stations(config);
        }

        // Oracle: HBT and optical comb keep Earth stations only (coherence on the ground).
        const std::string instrument = fixture.at("instrument").get<std::string>();
        if (instrument == "hbt" || instrument == "comb")
        {
            std::erase_if(stations, [](const Station& station) { return station.body != Body::Earth; });
        }

        return stations;
    }

    struct PositionDivergence
    {
        double max_relative = 0.0;
        double max_absolute_m = 0.0;
        std::size_t sample = 0;
        std::size_t station = 0;
    };

    [[nodiscard]] PositionDivergence compare_positions(const json& fixture, const std::vector<Station>& stations)
    {
        const json& times = fixture.at("sampleTimesHours");
        const json& expected = fixture.at("stationPositionsPerSampleM");
        PositionDivergence worst;

        for (std::size_t k = 0; k < times.size(); ++k)
        {
            // Sample times are exported as hours from the track centre, exactly as the oracle
            // passes them to stationState.
            const double t_hours = times.at(k).get<double>();
            for (std::size_t s = 0; s < stations.size(); ++s)
            {
                const json& p = expected.at(k).at(s);
                const Vec3d reference {p.at(0).get<double>(), p.at(1).get<double>(), p.at(2).get<double>()};
                const Vec3d computed = parallax::interferometry::station_state(stations[s], t_hours).position;

                const double absolute = glm::length(computed - reference);
                const double relative = absolute / glm::length(reference);
                worst.max_absolute_m = std::max(worst.max_absolute_m, absolute);
                if (relative > worst.max_relative)
                {
                    worst.max_relative = relative;
                    worst.sample = k;
                    worst.station = s;
                }
            }
        }

        return worst;
    }
}

TEST_CASE("Fixture ephemeris constants match the C++ ephemeris")
{
    namespace itf = parallax::interferometry;
    const json& all = fixtures();

    for (std::size_t f = 0; f < all.size(); ++f)
    {
        const json& eph = all.at(f).at("ephemeris");
        CAPTURE(f);

        CHECK(relative_error(itf::kOmegaE, eph.at("omegaEarthRadPerHour").get<double>()) <= kConstantRelTol);
        CHECK(relative_error(itf::kOmegaM, eph.at("omegaMoonRadPerHour").get<double>()) <= kConstantRelTol);
        CHECK(relative_error(itf::kDMoon, eph.at("moonDistanceM").get<double>()) <= kConstantRelTol);
        CHECK(relative_error(itf::kRMoon, eph.at("moonRadiusM").get<double>()) <= kConstantRelTol);
        CHECK(relative_error(itf::kIncMoon, eph.at("moonInclRad").get<double>()) <= kConstantRelTol);
        CHECK(relative_error(itf::kMoonPhase0, eph.at("moonPhase0Rad").get<double>()) <= kConstantRelTol);
        CHECK(relative_error(itf::kREarth, eph.at("earthRadiusM").get<double>()) <= kConstantRelTol);
        CHECK(relative_error(itf::kTychoLat, eph.at("tychoLatRad").get<double>()) <= kConstantRelTol);
        CHECK(relative_error(itf::kTychoLon, eph.at("tychoLonRad").get<double>()) <= kConstantRelTol);
    }
}

TEST_CASE("Station positions match stationPositionsPerSampleM for every fixture")
{
    const json& all = fixtures();

    for (std::size_t f = 0; f < all.size(); ++f)
    {
        const json& fixture = all.at(f);
        const std::string scenario = fixture.at("scenario").get<std::string>();
        CAPTURE(f);
        INFO("scenario: " << scenario);

        const std::vector<Station> stations = build_fixture_stations(fixture);
        const json& descriptors = fixture.at("stations");
        const json& expected = fixture.at("stationPositionsPerSampleM");

        const bool shape_ok = descriptors.size() == stations.size()
            && expected.size() == fixture.at("sampleTimesHours").size()
            && std::all_of(expected.begin(), expected.end(),
                [&stations](const json& row) { return row.size() == stations.size(); });
        CHECK_MESSAGE(shape_ok, "station count: C++ " << stations.size() << ", oracle " << descriptors.size());
        if (!shape_ok)
        {
            continue;
        }

        const PositionDivergence worst = compare_positions(fixture, stations);
        const std::string code = descriptors.at(worst.station).at("code").get<std::string>();

        MESSAGE("fixture " << f << " " << scenario << ": max rel " << worst.max_relative
                << ", max abs " << worst.max_absolute_m << " m (station " << code
                << ", sample " << worst.sample << ")");
        CHECK_MESSAGE(worst.max_relative <= kPositionRelTol,
            "worst station " << code << " at sample " << worst.sample
            << ": relative " << worst.max_relative << ", absolute " << worst.max_absolute_m << " m");
    }
}

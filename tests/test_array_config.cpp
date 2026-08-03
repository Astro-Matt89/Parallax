#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "core/types.hpp"
#include "interferometry/array_config.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace
{
    using parallax::astro_constants::kDegToRad;
    using parallax::astro_constants::kPi;
    using parallax::interferometry::ArrayConfig;
    using parallax::interferometry::ArrayGeometry;
    using parallax::interferometry::Body;
    using parallax::interferometry::SiteCenter;
    using parallax::interferometry::Station;

    constexpr double kTol = 1.0e-12;
    constexpr double kArmStepRadians = 2.0944;

    [[nodiscard]] bool approx(double lhs, double rhs, double tol = kTol)
    {
        return std::abs(lhs - rhs) <= tol;
    }

    [[nodiscard]] double wrap_angle(double angle)
    {
        double wrapped = std::fmod(angle, 2.0 * kPi);
        if (wrapped < 0.0)
        {
            wrapped += 2.0 * kPi;
        }
        return wrapped;
    }

    [[nodiscard]] double shortest_angle_delta(double a, double b)
    {
        const double diff = std::fmod(a - b + kPi, 2.0 * kPi);
        return std::abs((diff < 0.0 ? diff + 2.0 * kPi : diff) - kPi);
    }

    [[nodiscard]] double radius_for(Body body)
    {
        return (body == Body::Moon) ? parallax::interferometry::kRMoon : parallax::interferometry::kREarth;
    }

    [[nodiscard]] std::vector<parallax::Vec2d> normalized_xy(
        const std::vector<Station>& stations,
        const SiteCenter& site,
        double site_extent_m)
    {
        std::vector<parallax::Vec2d> normalized;
        normalized.reserve(stations.size());

        const double half_extent = site_extent_m / 2.0;
        const double radius = radius_for(site.body);

        for (const Station& station : stations)
        {
            const double x_norm = ((station.lon - site.lon) * radius) / half_extent;
            const double y_norm = ((station.lat - site.lat) * radius) / half_extent;
            normalized.emplace_back(x_norm, y_norm);
        }

        return normalized;
    }

    [[nodiscard]] double max_baseline_from_offsets(
        const std::vector<Station>& stations,
        const SiteCenter& site)
    {
        const double radius = radius_for(site.body);

        std::vector<parallax::Vec2d> offsets;
        offsets.reserve(stations.size());
        for (const Station& station : stations)
        {
            offsets.emplace_back(
                (station.lon - site.lon) * radius,
                (station.lat - site.lat) * radius);
        }

        double max_baseline = 0.0;
        for (std::size_t i = 0; i < offsets.size(); ++i)
        {
            for (std::size_t j = i + 1; j < offsets.size(); ++j)
            {
                const double baseline = glm::length(offsets[i] - offsets[j]);
                max_baseline = std::max(max_baseline, baseline);
            }
        }
        return max_baseline;
    }
}

TEST_CASE("Y array default 10 km config produces 13 stations")
{
    ArrayConfig config;
    config.geometry = ArrayGeometry::Y;
    config.antennas_per_arm = 4;
    config.site_extent_m = 10000.0;
    config.site = SiteCenter {
        .body = Body::Moon,
        .lat = -43.3 * kDegToRad,
        .lon = -11.2 * kDegToRad,
    };

    const std::vector<Station> stations = parallax::interferometry::generate_stations(config);

    CHECK(stations.size() == 13u);
    CHECK(stations.front().name == "GW-Y-A0-1");
    CHECK(stations.back().name == "GW-Y-C");
}

TEST_CASE("Y array geometry matches sandbox arm bearings and radial profile")
{
    ArrayConfig config;
    config.geometry = ArrayGeometry::Y;
    config.antennas_per_arm = 4;
    config.site_extent_m = 10000.0;
    config.site = SiteCenter {
        .body = Body::Moon,
        .lat = -43.3 * kDegToRad,
        .lon = -11.2 * kDegToRad,
    };

    const std::vector<Station> stations = parallax::interferometry::generate_stations(config);
    REQUIRE(stations.size() == 13u);

    const std::vector<parallax::Vec2d> xy = normalized_xy(stations, config.site, config.site_extent_m);

    for (std::uint32_t arm = 0; arm < 3; ++arm)
    {
        const double expected_bearing = wrap_angle(static_cast<double>(arm) * kArmStepRadians - std::numbers::pi_v<double> / 2.0);
        double prev_r = 0.0;

        for (std::uint32_t i = 0; i < config.antennas_per_arm; ++i)
        {
            const std::size_t idx = static_cast<std::size_t>(arm * config.antennas_per_arm + i);
            const double bearing = wrap_angle(std::atan2(xy[idx].y, xy[idx].x));
            const double radius = std::hypot(xy[idx].x, xy[idx].y);
            const double expected_r = std::pow((static_cast<double>(i + 1u) / static_cast<double>(config.antennas_per_arm)), 1.7) * 0.85;

            CHECK(shortest_angle_delta(bearing, expected_bearing) < 1.0e-12);
            CHECK(approx(radius, expected_r, 1.0e-12));
            CHECK(radius > prev_r);
            prev_r = radius;
        }
    }

    const double arm0 = wrap_angle(std::atan2(xy[0].y, xy[0].x));
    const double arm1 = wrap_angle(std::atan2(xy[4].y, xy[4].x));
    const double arm2 = wrap_angle(std::atan2(xy[8].y, xy[8].x));
    CHECK(approx(shortest_angle_delta(arm1, arm0), kArmStepRadians, 1.0e-12));
    CHECK(approx(shortest_angle_delta(arm2, arm1), kArmStepRadians, 1.0e-12));
}

TEST_CASE("Y array central station equals site center")
{
    ArrayConfig config;
    config.geometry = ArrayGeometry::Y;
    config.antennas_per_arm = 4;
    config.site_extent_m = 10000.0;
    config.site = SiteCenter {
        .body = Body::Moon,
        .lat = -43.3 * kDegToRad,
        .lon = -11.2 * kDegToRad,
    };

    const std::vector<Station> stations = parallax::interferometry::generate_stations(config);
    REQUIRE(stations.size() == 13u);

    const Station& center = stations.back();
    CHECK(approx(center.lat, config.site.lat, 0.0));
    CHECK(approx(center.lon, config.site.lon, 0.0));
}

TEST_CASE("Site extent scales max baseline in 1:10:100 ratio")
{
    ArrayConfig config;
    config.geometry = ArrayGeometry::Y;
    config.antennas_per_arm = 4;
    config.site = SiteCenter {
        .body = Body::Moon,
        .lat = -43.3 * kDegToRad,
        .lon = -11.2 * kDegToRad,
    };

    config.site_extent_m = 1000.0;
    const std::vector<Station> stations_1km = parallax::interferometry::generate_stations(config);
    const double baseline_1km = max_baseline_from_offsets(stations_1km, config.site);

    config.site_extent_m = 10000.0;
    const std::vector<Station> stations_10km = parallax::interferometry::generate_stations(config);
    const double baseline_10km = max_baseline_from_offsets(stations_10km, config.site);

    config.site_extent_m = 100000.0;
    const std::vector<Station> stations_100km = parallax::interferometry::generate_stations(config);
    const double baseline_100km = max_baseline_from_offsets(stations_100km, config.site);

    REQUIRE(baseline_1km > 0.0);
    CHECK(approx(baseline_10km / baseline_1km, 10.0, 1.0e-10));
    CHECK(approx(baseline_100km / baseline_1km, 100.0, 1.0e-10));
}

TEST_CASE("Antenna count is fully data-driven")
{
    ArrayConfig config;
    config.geometry = ArrayGeometry::Y;
    config.antennas_per_arm = 7;
    config.site_extent_m = 10000.0;
    config.site = SiteCenter {
        .body = Body::Moon,
        .lat = -43.3 * kDegToRad,
        .lon = -11.2 * kDegToRad,
    };

    const std::vector<Station> stations = parallax::interferometry::generate_stations(config);
    CHECK(stations.size() == 22u);
}

TEST_CASE("Array config JSON round trip preserves fields and angle boundary")
{
    ArrayConfig config;
    config.geometry = ArrayGeometry::Grid;
    config.antennas_per_arm = 5;
    config.site_extent_m = 100000.0;
    config.station_aperture_m = 8.25;
    config.available_bands = {"Visible", "RadioK"};
    config.site = SiteCenter {
        .body = Body::Earth,
        .lat = 19.8206 * kDegToRad,
        .lon = -155.4681 * kDegToRad,
    };
    config.custom_stations = {
        Station {
            .name = "CustomA",
            .body = Body::Moon,
            .lat = -12.5 * kDegToRad,
            .lon = 44.25 * kDegToRad,
        },
    };

    const nlohmann::json json = parallax::interferometry::to_json(config);
    CHECK(approx(json.at("site").at("lat_deg").get<double>(), 19.8206));
    CHECK(approx(json.at("site").at("lon_deg").get<double>(), -155.4681));

    const std::optional<ArrayConfig> parsed = parallax::interferometry::from_json(json);
    REQUIRE(parsed.has_value());

    const ArrayConfig& loaded = parsed.value();
    CHECK(loaded.geometry == config.geometry);
    CHECK(loaded.antennas_per_arm == config.antennas_per_arm);
    CHECK(approx(loaded.site_extent_m, config.site_extent_m));
    CHECK(approx(loaded.station_aperture_m, config.station_aperture_m));
    CHECK(loaded.available_bands == config.available_bands);
    CHECK(loaded.site.body == config.site.body);
    CHECK(approx(loaded.site.lat, config.site.lat));
    CHECK(approx(loaded.site.lon, config.site.lon));
    REQUIRE(loaded.custom_stations.size() == config.custom_stations.size());
    CHECK(loaded.custom_stations[0].name == config.custom_stations[0].name);
    CHECK(loaded.custom_stations[0].body == config.custom_stations[0].body);
    CHECK(approx(loaded.custom_stations[0].lat, config.custom_stations[0].lat));
    CHECK(approx(loaded.custom_stations[0].lon, config.custom_stations[0].lon));
}

TEST_CASE("Malformed/missing field JSON returns nullopt")
{
    const nlohmann::json missing_fields = {
        {"geometry", "Y"},
        {"site_extent_m", 10000.0},
    };

    const nlohmann::json malformed_types = {
        {"geometry", "Y"},
        {"antennas_per_arm", "four"},
        {"site_extent_m", 10000.0},
        {"station_aperture_m", 12.0},
        {"available_bands", nlohmann::json::array({"Visible"})},
        {"site", {{"body", "Moon"}, {"lat_deg", -43.3}, {"lon_deg", -11.2}}},
    };

    CHECK_FALSE(parallax::interferometry::from_json(missing_fields).has_value());
    CHECK_FALSE(parallax::interferometry::from_json(malformed_types).has_value());
}

TEST_CASE("earth_stations returns fixed Earth locations")
{
    const std::vector<Station> stations = parallax::interferometry::earth_stations();
    REQUIRE(stations.size() == 3u);

    for (const Station& station : stations)
    {
        CHECK(station.body == Body::Earth);
        CHECK(station.lat >= -std::numbers::pi_v<double> / 2.0);
        CHECK(station.lat <= std::numbers::pi_v<double> / 2.0);
        CHECK(station.lon >= -std::numbers::pi_v<double>);
        CHECK(station.lon <= std::numbers::pi_v<double>);
    }

    CHECK(stations[0].name == "La Palma");
    CHECK(stations[1].name == "Mauna Kea");
    CHECK(stations[2].name == "Paranal");
}

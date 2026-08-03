#include "interferometry/array_config.hpp"

#include "core/types.hpp"

#include <spdlog/spdlog.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace parallax::interferometry
{
    namespace
    {
        constexpr int kSchemaVersion = 1;
        constexpr std::string_view kSchema = "parallax.interferometry.array_config";
        constexpr double kArmStepRadians = 2.0944; // Sandbox oracle literal; do not replace with 2*pi/3.
        constexpr double kArmRadiusScale = 0.85;

        [[nodiscard]] double rad_to_deg(double radians)
        {
            return radians * astro_constants::kRadToDeg;
        }

        [[nodiscard]] double deg_to_rad(double degrees)
        {
            return degrees * astro_constants::kDegToRad;
        }

        [[nodiscard]] std::string geometry_to_string(ArrayGeometry geometry)
        {
            switch (geometry)
            {
                case ArrayGeometry::Y: return "Y";
                case ArrayGeometry::Ring: return "Ring";
                case ArrayGeometry::Grid: return "Grid";
                case ArrayGeometry::Custom: return "Custom";
            }
            return "Y";
        }

        [[nodiscard]] ArrayGeometry geometry_from_string(std::string_view geometry)
        {
            if (geometry == "Y") { return ArrayGeometry::Y; }
            if (geometry == "Ring") { return ArrayGeometry::Ring; }
            if (geometry == "Grid") { return ArrayGeometry::Grid; }
            if (geometry == "Custom") { return ArrayGeometry::Custom; }

            spdlog::warn("[ArrayConfig] unknown geometry \"{}\"; falling back to Y", std::string(geometry));
            return ArrayGeometry::Y;
        }

        [[nodiscard]] std::string body_to_string(Body body)
        {
            switch (body)
            {
                case Body::Earth: return "Earth";
                case Body::Moon: return "Moon";
            }
            return "Moon";
        }

        [[nodiscard]] Body body_from_string(std::string_view body)
        {
            if (body == "Earth") { return Body::Earth; }
            if (body == "Moon") { return Body::Moon; }

            spdlog::warn("[ArrayConfig] unknown body \"{}\"; falling back to Moon", std::string(body));
            return Body::Moon;
        }

        [[nodiscard]] double body_radius(Body body)
        {
            return (body == Body::Moon) ? kRMoon : kREarth;
        }

        [[nodiscard]] bool is_supported_site_extent(double site_extent_m)
        {
            constexpr double kTol = 1.0e-9;
            return std::abs(site_extent_m - kSiteExtent1Km) <= kTol
                || std::abs(site_extent_m - kSiteExtent10Km) <= kTol
                || std::abs(site_extent_m - kSiteExtent100Km) <= kTol;
        }

        [[nodiscard]] std::vector<Vec2d> make_y_layout(std::uint32_t antennas_per_arm)
        {
            std::vector<Vec2d> normalized_stations;
            normalized_stations.reserve(3u * antennas_per_arm + 1u);

            const double n = static_cast<double>(antennas_per_arm);
            for (std::uint32_t arm = 0; arm < 3; ++arm)
            {
                // Oracle: glasswing-sandbox-v1_7.html Y preset.
                const double a = static_cast<double>(arm) * kArmStepRadians - astro_constants::kHalfPi;
                for (std::uint32_t i = 1; i <= antennas_per_arm; ++i)
                {
                    // Oracle radial profile: r = pow(i / n, 1.7) * 0.85.
                    const double r = std::pow(static_cast<double>(i) / n, 1.7) * kArmRadiusScale;
                    normalized_stations.push_back(Vec2d {
                        std::cos(a) * r,
                        std::sin(a) * r,
                    });
                }
            }

            normalized_stations.push_back(Vec2d {0.0, 0.0});
            return normalized_stations;
        }

        [[nodiscard]] std::vector<Vec2d> make_ring_layout(std::uint32_t antennas_per_arm)
        {
            const std::uint32_t ring_count = 3u * antennas_per_arm;
            std::vector<Vec2d> normalized_stations;
            normalized_stations.reserve(ring_count + 1u);

            for (std::uint32_t i = 0; i < ring_count; ++i)
            {
                const double t = astro_constants::kTwoPi * (static_cast<double>(i) / static_cast<double>(ring_count));
                normalized_stations.push_back(Vec2d {
                    kArmRadiusScale * std::cos(t),
                    kArmRadiusScale * std::sin(t),
                });
            }

            normalized_stations.push_back(Vec2d {0.0, 0.0});
            return normalized_stations;
        }

        [[nodiscard]] std::vector<Vec2d> make_grid_layout(std::uint32_t antennas_per_arm)
        {
            const std::uint32_t total = 3u * antennas_per_arm + 1u;
            const std::uint32_t side = static_cast<std::uint32_t>(std::ceil(std::sqrt(static_cast<double>(total))));
            std::vector<Vec2d> normalized_stations;
            normalized_stations.reserve(total);

            for (std::uint32_t row = 0; row < side && normalized_stations.size() < total; ++row)
            {
                for (std::uint32_t col = 0; col < side && normalized_stations.size() < total; ++col)
                {
                    const double x = (side == 1u)
                        ? 0.0
                        : -kArmRadiusScale + (2.0 * kArmRadiusScale * static_cast<double>(col)
                                              / static_cast<double>(side - 1u));
                    const double y = (side == 1u)
                        ? 0.0
                        : -kArmRadiusScale + (2.0 * kArmRadiusScale * static_cast<double>(row)
                                              / static_cast<double>(side - 1u));
                    normalized_stations.push_back(Vec2d {x, y});
                }
            }

            return normalized_stations;
        }

        [[nodiscard]] std::string station_name(ArrayGeometry geometry, std::size_t index, std::uint32_t antennas_per_arm)
        {
            if (geometry == ArrayGeometry::Y)
            {
                const std::size_t arm_station_count = static_cast<std::size_t>(3u * antennas_per_arm);
                if (index == arm_station_count)
                {
                    return "GW-Y-C";
                }

                const std::size_t arm = index / antennas_per_arm;
                const std::size_t arm_index = (index % antennas_per_arm) + 1u;
                return "GW-Y-A" + std::to_string(arm) + "-" + std::to_string(arm_index);
            }

            if (geometry == ArrayGeometry::Ring)
            {
                return "GW-R-" + std::to_string(index);
            }

            if (geometry == ArrayGeometry::Grid)
            {
                return "GW-G-" + std::to_string(index);
            }

            return "GW-C-" + std::to_string(index);
        }

        [[nodiscard]] std::vector<Station> build_stations_from_normalized(
            const std::vector<Vec2d>& normalized_stations,
            const ArrayConfig& config)
        {
            std::vector<Station> stations;
            stations.reserve(normalized_stations.size());

            const double half_extent_m = config.site_extent_m / 2.0;
            const double radius = body_radius(config.site.body);

            for (std::size_t i = 0; i < normalized_stations.size(); ++i)
            {
                // Preserve oracle buildStations expression shape/order for fixture compatibility:
                // x_m = x_norm * (site_extent_m / 2), y_m = y_norm * (site_extent_m / 2),
                // lat = site.lat + y_m / R, lon = site.lon + x_m / R.
                const double x_m = normalized_stations[i].x * half_extent_m;
                const double y_m = normalized_stations[i].y * half_extent_m;

                const double lat = config.site.lat + y_m / radius;
                const double lon = config.site.lon + x_m / radius;

                stations.push_back(Station {
                    .name = station_name(config.geometry, i, config.antennas_per_arm),
                    .body = config.site.body,
                    .lat = lat,
                    .lon = lon,
                });
            }

            return stations;
        }

        [[nodiscard]] nlohmann::json station_to_json(const Station& station)
        {
            nlohmann::json json;
            json["name"] = station.name;
            json["body"] = body_to_string(station.body);
            json["lat_deg"] = rad_to_deg(station.lat);
            json["lon_deg"] = rad_to_deg(station.lon);
            return json;
        }
    }

    std::vector<Station> generate_stations(const ArrayConfig& config)
    {
        if (config.geometry == ArrayGeometry::Custom)
        {
            return config.custom_stations;
        }

        if (config.antennas_per_arm == 0)
        {
            spdlog::error("[ArrayConfig] antennas_per_arm must be > 0 for generated geometries");
            return {};
        }

        if (config.site_extent_m <= 0.0)
        {
            spdlog::error("[ArrayConfig] site_extent_m must be positive");
            return {};
        }

        if (!is_supported_site_extent(config.site_extent_m))
        {
            spdlog::warn(
                "[ArrayConfig] site_extent_m={} is outside 1/10/100 km presets; allowing for upgrades",
                config.site_extent_m);
        }

        if (config.geometry == ArrayGeometry::Y)
        {
            return build_stations_from_normalized(make_y_layout(config.antennas_per_arm), config);
        }

        if (config.geometry == ArrayGeometry::Ring)
        {
            return build_stations_from_normalized(make_ring_layout(config.antennas_per_arm), config);
        }

        return build_stations_from_normalized(make_grid_layout(config.antennas_per_arm), config);
    }

    std::vector<Station> earth_stations()
    {
        return {
            Station {
                .name = "La Palma",
                .body = Body::Earth,
                .lat = 28.7569 * astro_constants::kDegToRad,
                .lon = -17.8925 * astro_constants::kDegToRad,
            },
            Station {
                .name = "Mauna Kea",
                .body = Body::Earth,
                .lat = 19.8206 * astro_constants::kDegToRad,
                .lon = -155.4681 * astro_constants::kDegToRad,
            },
            Station {
                .name = "Paranal",
                .body = Body::Earth,
                .lat = -24.6275 * astro_constants::kDegToRad,
                .lon = -70.4044 * astro_constants::kDegToRad,
            },
        };
    }

    void append_earth_stations(std::vector<Station>& stations)
    {
        const std::vector<Station> fixed_earth_stations = earth_stations();
        stations.insert(stations.end(), fixed_earth_stations.begin(), fixed_earth_stations.end());
    }

    nlohmann::json to_json(const ArrayConfig& config)
    {
        nlohmann::json json;
        json["schema"] = kSchema;
        json["version"] = kSchemaVersion;
        json["geometry"] = geometry_to_string(config.geometry);
        json["antennas_per_arm"] = config.antennas_per_arm;
        json["site_extent_m"] = config.site_extent_m;
        json["station_aperture_m"] = config.station_aperture_m;
        json["available_bands"] = config.available_bands;
        // File boundary uses degrees for human editability; runtime stays radians.
        json["site"] = {
            {"body", body_to_string(config.site.body)},
            {"lat_deg", rad_to_deg(config.site.lat)},
            {"lon_deg", rad_to_deg(config.site.lon)},
        };

        nlohmann::json custom = nlohmann::json::array();
        for (const Station& station : config.custom_stations)
        {
            custom.push_back(station_to_json(station));
        }
        json["custom_stations"] = std::move(custom);

        return json;
    }

    std::optional<ArrayConfig> from_json(const nlohmann::json& json)
    {
        try
        {
            if (!json.is_object())
            {
                spdlog::error("[ArrayConfig] root JSON is not an object");
                return std::nullopt;
            }

            if (!json.contains("geometry")
                || !json.contains("antennas_per_arm")
                || !json.contains("site_extent_m")
                || !json.contains("station_aperture_m")
                || !json.contains("available_bands")
                || !json.contains("site"))
            {
                spdlog::error("[ArrayConfig] missing required fields");
                return std::nullopt;
            }

            ArrayConfig config;
            config.geometry = geometry_from_string(json.at("geometry").get<std::string>());
            config.antennas_per_arm = json.at("antennas_per_arm").get<std::uint32_t>();
            config.site_extent_m = json.at("site_extent_m").get<double>();
            config.station_aperture_m = json.at("station_aperture_m").get<double>();
            config.available_bands = json.at("available_bands").get<std::vector<std::string>>();

            if (config.antennas_per_arm == 0)
            {
                spdlog::error("[ArrayConfig] antennas_per_arm must be > 0");
                return std::nullopt;
            }

            if (config.site_extent_m <= 0.0)
            {
                spdlog::error("[ArrayConfig] site_extent_m must be positive");
                return std::nullopt;
            }

            const nlohmann::json& site = json.at("site");
            if (!site.is_object() || !site.contains("body") || !site.contains("lat_deg") || !site.contains("lon_deg"))
            {
                spdlog::error("[ArrayConfig] missing required site fields");
                return std::nullopt;
            }

            config.site.body = body_from_string(site.at("body").get<std::string>());
            // Convert degrees -> radians immediately at load boundary.
            config.site.lat = deg_to_rad(site.at("lat_deg").get<double>());
            config.site.lon = deg_to_rad(site.at("lon_deg").get<double>());

            config.custom_stations.clear();
            if (json.contains("custom_stations"))
            {
                const nlohmann::json& custom = json.at("custom_stations");
                if (!custom.is_array())
                {
                    spdlog::error("[ArrayConfig] custom_stations must be an array");
                    return std::nullopt;
                }

                config.custom_stations.reserve(custom.size());
                for (const auto& station_json : custom)
                {
                    if (!station_json.is_object()
                        || !station_json.contains("name")
                        || !station_json.contains("body")
                        || !station_json.contains("lat_deg")
                        || !station_json.contains("lon_deg"))
                    {
                        spdlog::error("[ArrayConfig] malformed custom station entry");
                        return std::nullopt;
                    }

                    config.custom_stations.push_back(Station {
                        .name = station_json.at("name").get<std::string>(),
                        .body = body_from_string(station_json.at("body").get<std::string>()),
                        .lat = deg_to_rad(station_json.at("lat_deg").get<double>()),
                        .lon = deg_to_rad(station_json.at("lon_deg").get<double>()),
                    });
                }
            }

            return config;
        }
        catch (const std::exception& ex)
        {
            spdlog::error("[ArrayConfig] failed to parse JSON: {}", ex.what());
            return std::nullopt;
        }
    }

    std::optional<ArrayConfig> load_array_config(const std::filesystem::path& path)
    {
        std::ifstream stream(path);
        if (!stream.is_open())
        {
            spdlog::error("[ArrayConfig] failed to open config file: {}", path.string());
            return std::nullopt;
        }

        nlohmann::json json = nlohmann::json::parse(stream, nullptr, false);
        if (json.is_discarded())
        {
            spdlog::error("[ArrayConfig] failed to parse config JSON: {}", path.string());
            return std::nullopt;
        }

        if (json.contains("version"))
        {
            const int version = json.value("version", 0);
            if (version != kSchemaVersion)
            {
                spdlog::error("[ArrayConfig] unsupported schema version {} in {}", version, path.string());
                return std::nullopt;
            }
        }

        return from_json(json);
    }

    bool save_array_config(const ArrayConfig& config, const std::filesystem::path& path)
    {
        const std::filesystem::path parent = path.parent_path();
        if (!parent.empty())
        {
            std::error_code error_code;
            std::filesystem::create_directories(parent, error_code);
            if (error_code)
            {
                spdlog::error(
                    "[ArrayConfig] failed to create directory {}: {}",
                    parent.string(),
                    error_code.message());
                return false;
            }
        }

        std::ofstream stream(path);
        if (!stream.is_open())
        {
            spdlog::error("[ArrayConfig] failed to open config for writing: {}", path.string());
            return false;
        }

        stream << to_json(config).dump(4);
        if (!stream.good())
        {
            spdlog::error("[ArrayConfig] failed while writing config: {}", path.string());
            return false;
        }

        return true;
    }
}

#pragma once

#include "interferometry/ephemeris.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace parallax::interferometry
{
    enum class ArrayGeometry
    {
        Y,
        Ring,
        Grid,
        Custom,
    };

    struct SiteCenter
    {
        Body body = Body::Moon;
        double lat = kTychoLat; // radians
        double lon = kTychoLon; // radians
    };

    struct ArrayConfig
    {
        ArrayGeometry geometry = ArrayGeometry::Y;
        std::uint32_t antennas_per_arm = 4;
        double site_extent_m = 10000.0;
        double station_aperture_m = 12.0;
        std::vector<std::string> available_bands = {"Visible", "IR", "MidIR", "RadioK", "Submm"};
        SiteCenter site;
        std::vector<Station> custom_stations;
    };

    inline constexpr double kSiteExtent1Km = 1000.0;
    inline constexpr double kSiteExtent10Km = 10000.0;
    inline constexpr double kSiteExtent100Km = 100000.0;

    [[nodiscard]] std::vector<Station> generate_stations(const ArrayConfig& config);

    [[nodiscard]] std::vector<Station> earth_stations();

    void append_earth_stations(std::vector<Station>& stations);

    [[nodiscard]] nlohmann::json to_json(const ArrayConfig& config);
    [[nodiscard]] std::optional<ArrayConfig> from_json(const nlohmann::json& json);
    [[nodiscard]] std::optional<ArrayConfig> load_array_config(const std::filesystem::path& path);
    bool save_array_config(const ArrayConfig& config, const std::filesystem::path& path);
}

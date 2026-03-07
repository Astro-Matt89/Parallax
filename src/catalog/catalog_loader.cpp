/// @file catalog_loader.cpp
/// @brief Implementation of CSV star catalog loaders.

#include "catalog/catalog_loader.hpp"

#include "core/logger.hpp"
#include "core/types.hpp"

#include <charconv>
#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace parallax::catalog
{

// -----------------------------------------------------------------
// Load bright-star CSV: Name,RA_deg,Dec_deg,Vmag,BV
// -----------------------------------------------------------------

std::optional<std::vector<StarEntry>>
CatalogLoader::load_bright_star_csv(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        PLX_CORE_ERROR("CatalogLoader: Failed to open file: {}", path.string());
        return std::nullopt;
    }

    std::vector<StarEntry> stars;
    std::string line;

    // Skip header line
    if (!std::getline(file, line))
    {
        PLX_CORE_ERROR("CatalogLoader: File is empty: {}", path.string());
        return std::nullopt;
    }

    u32 line_number = 1;
    u32 skipped = 0;

    while (std::getline(file, line))
    {
        ++line_number;

        if (line.empty())
        {
            continue;
        }

        // Parse CSV columns: Name,RA_deg,Dec_deg,Vmag,BV
        std::istringstream stream(line);
        std::string name_str;
        std::string ra_str;
        std::string dec_str;
        std::string mag_str;
        std::string bv_str;

        if (!std::getline(stream, name_str, ',') ||
            !std::getline(stream, ra_str, ',') ||
            !std::getline(stream, dec_str, ',') ||
            !std::getline(stream, mag_str, ',') ||
            !std::getline(stream, bv_str))
        {
            PLX_CORE_WARN("CatalogLoader: Malformed line {}: {}", line_number, line);
            ++skipped;
            continue;
        }

        const auto ra_deg  = parse_f64(trim(ra_str));
        const auto dec_deg = parse_f64(trim(dec_str));
        const auto mag_v   = parse_f64(trim(mag_str));
        const auto bv      = parse_f64(trim(bv_str));

        if (!ra_deg || !dec_deg || !mag_v || !bv)
        {
            PLX_CORE_WARN("CatalogLoader: Failed to parse values on line {}: {}",
                          line_number, line);
            ++skipped;
            continue;
        }

        stars.push_back(StarEntry{
            .ra         = *ra_deg * astro_constants::kDegToRad,
            .dec        = *dec_deg * astro_constants::kDegToRad,
            .mag_v      = static_cast<f32>(*mag_v),
            .color_bv   = static_cast<f32>(*bv),
            .catalog_id = line_number - 1,  // 1-based index (line 2 = star 1)
        });
    }

    if (stars.empty())
    {
        PLX_CORE_ERROR("CatalogLoader: No valid stars found in: {}", path.string());
        return std::nullopt;
    }

    if (skipped > 0)
    {
        PLX_CORE_WARN("CatalogLoader: Skipped {} malformed lines", skipped);
    }

    PLX_CORE_INFO("CatalogLoader: Loaded {} stars from {}", stars.size(), path.string());

    return stars;
}

// -----------------------------------------------------------------
// Load Hipparcos CSV: HIP,RA_deg,Dec_deg,Vmag,BV
// -----------------------------------------------------------------

std::optional<std::vector<StarEntry>>
CatalogLoader::load_hipparcos_csv(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        PLX_CORE_ERROR("CatalogLoader: Failed to open file: {}", path.string());
        return std::nullopt;
    }

    std::vector<StarEntry> stars;
    std::string line;

    // Skip header line
    if (!std::getline(file, line))
    {
        PLX_CORE_ERROR("CatalogLoader: File is empty: {}", path.string());
        return std::nullopt;
    }

    u32 line_number = 1;
    u32 skipped = 0;

    while (std::getline(file, line))
    {
        ++line_number;

        if (line.empty())
        {
            continue;
        }

        // Parse CSV columns: HIP,RA_deg,Dec_deg,Vmag,BV
        std::istringstream stream(line);
        std::string hip_str;
        std::string ra_str;
        std::string dec_str;
        std::string mag_str;
        std::string bv_str;

        if (!std::getline(stream, hip_str, ',') ||
            !std::getline(stream, ra_str, ',') ||
            !std::getline(stream, dec_str, ',') ||
            !std::getline(stream, mag_str, ',') ||
            !std::getline(stream, bv_str))
        {
            PLX_CORE_WARN("CatalogLoader: Malformed line {}: {}", line_number, line);
            ++skipped;
            continue;
        }

        const auto hip_id  = parse_u32(trim(hip_str));
        const auto ra_deg  = parse_f64(trim(ra_str));
        const auto dec_deg = parse_f64(trim(dec_str));
        const auto mag_v   = parse_f64(trim(mag_str));
        const auto bv      = parse_f64(trim(bv_str));

        if (!hip_id || !ra_deg || !dec_deg || !mag_v || !bv)
        {
            PLX_CORE_WARN("CatalogLoader: Failed to parse values on line {}: {}",
                          line_number, line);
            ++skipped;
            continue;
        }

        stars.push_back(StarEntry{
            .ra         = *ra_deg * astro_constants::kDegToRad,
            .dec        = *dec_deg * astro_constants::kDegToRad,
            .mag_v      = static_cast<f32>(*mag_v),
            .color_bv   = static_cast<f32>(*bv),
            .catalog_id = *hip_id,
        });
    }

    if (stars.empty())
    {
        PLX_CORE_ERROR("CatalogLoader: No valid stars found in: {}", path.string());
        return std::nullopt;
    }

    if (skipped > 0)
    {
        PLX_CORE_WARN("CatalogLoader: Skipped {} malformed lines", skipped);
    }

    PLX_CORE_INFO("CatalogLoader: Loaded {} stars from {}", stars.size(), path.string());

    return stars;
}

// -----------------------------------------------------------------
// Load Tycho-2 CSV: TYC,RA_deg,Dec_deg,Vmag,BV
// -----------------------------------------------------------------

std::optional<std::vector<StarEntry>>
CatalogLoader::load_tycho2_csv(const std::filesystem::path& path)
{
    using Clock = std::chrono::high_resolution_clock;
    const auto t_start = Clock::now();

    std::ifstream file(path);
    if (!file.is_open())
    {
        PLX_CORE_ERROR("CatalogLoader: Failed to open Tycho-2 file: {}", path.string());
        return std::nullopt;
    }

    std::vector<StarEntry> stars;
    stars.reserve(2600000);  // Tycho-2 has ~2.5M entries

    std::string line;

    // Skip header line
    if (!std::getline(file, line))
    {
        PLX_CORE_ERROR("CatalogLoader: Tycho-2 file is empty: {}", path.string());
        return std::nullopt;
    }

    u32 line_number = 1;
    u32 skipped = 0;

    while (std::getline(file, line))
    {
        ++line_number;

        if (line.empty())
        {
            continue;
        }

        // Parse CSV columns: TYC,RA_deg,Dec_deg,Vmag,BV
        std::istringstream stream(line);
        std::string tyc_str;
        std::string ra_str;
        std::string dec_str;
        std::string mag_str;
        std::string bv_str;

        if (!std::getline(stream, tyc_str, ',') ||
            !std::getline(stream, ra_str, ',') ||
            !std::getline(stream, dec_str, ',') ||
            !std::getline(stream, mag_str, ',') ||
            !std::getline(stream, bv_str))
        {
            ++skipped;
            continue;
        }

        const auto ra_deg  = parse_f64(trim(ra_str));
        const auto dec_deg = parse_f64(trim(dec_str));
        const auto mag_v   = parse_f64(trim(mag_str));
        const auto bv      = parse_f64(trim(bv_str));

        if (!ra_deg || !dec_deg || !mag_v || !bv)
        {
            ++skipped;
            continue;
        }

        stars.push_back(StarEntry{
            .ra         = *ra_deg * astro_constants::kDegToRad,
            .dec        = *dec_deg * astro_constants::kDegToRad,
            .mag_v      = static_cast<f32>(*mag_v),
            .color_bv   = static_cast<f32>(*bv),
            .catalog_id = hash_tyc_id(trim(tyc_str)),
        });
    }

    const auto t_end = Clock::now();
    const f64 elapsed_ms = std::chrono::duration<f64, std::milli>(t_end - t_start).count();

    if (stars.empty())
    {
        PLX_CORE_ERROR("CatalogLoader: No valid stars found in Tycho-2: {}", path.string());
        return std::nullopt;
    }

    if (skipped > 0)
    {
        PLX_CORE_WARN("CatalogLoader: Tycho-2 skipped {} malformed lines", skipped);
    }

    PLX_CORE_INFO("CatalogLoader: Loaded {} Tycho-2 stars from {} in {:.1f}ms",
                  stars.size(), path.string(), elapsed_ms);

    return stars;
}

// -----------------------------------------------------------------
// Utility: trim whitespace
// -----------------------------------------------------------------

std::string_view CatalogLoader::trim(std::string_view sv)
{
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r'))
    {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r'))
    {
        sv.remove_suffix(1);
    }
    return sv;
}

// -----------------------------------------------------------------
// Utility: parse f64 from string_view
// -----------------------------------------------------------------

std::optional<f64> CatalogLoader::parse_f64(std::string_view sv)
{
    if (sv.empty())
    {
        return std::nullopt;
    }

    // std::from_chars for double requires C++17 and MSVC/GCC 11+/Clang 16+
    f64 value = 0.0;
    const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), value);

    if (ec != std::errc{} || ptr != sv.data() + sv.size())
    {
        return std::nullopt;
    }

    return value;
}

// -----------------------------------------------------------------
// Utility: parse u32 from string_view
// -----------------------------------------------------------------

std::optional<u32> CatalogLoader::parse_u32(std::string_view sv)
{
    if (sv.empty())
    {
        return std::nullopt;
    }

    u32 value = 0;
    const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), value);

    if (ec != std::errc{} || ptr != sv.data() + sv.size())
    {
        return std::nullopt;
    }

    return value;
}

// -----------------------------------------------------------------
// Utility: hash TYC identifier string → u32
// -----------------------------------------------------------------

u32 CatalogLoader::hash_tyc_id(std::string_view tyc_str)
{
    // FNV-1a 32-bit hash
    u32 hash = 2166136261u;
    for (char c : tyc_str)
    {
        hash ^= static_cast<u32>(static_cast<u8>(c));
        hash *= 16777619u;
    }
    return hash;
}

} // namespace parallax::catalog
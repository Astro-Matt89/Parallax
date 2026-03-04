/// @file dso_loader.cpp
/// @brief DSO catalog CSV loader implementation.

#include "catalog/dso_loader.hpp"

#include "core/logger.hpp"

#include <charconv>
#include <fstream>
#include <sstream>
#include <string>

namespace parallax::catalog
{

std::optional<std::vector<DsoEntry>>
DsoLoader::load_messier_csv(const std::filesystem::path& path)
{
    std::ifstream file{path};
    if (!file.is_open())
    {
        PLX_CORE_ERROR("Cannot open DSO catalog: {}", path.string());
        return std::nullopt;
    }

    std::vector<DsoEntry> entries;
    entries.reserve(128);

    std::string line;
    u32 line_num = 0;
    u32 skipped = 0;

    while (std::getline(file, line))
    {
        ++line_num;

        // Skip header and comments
        if (line.empty() || line[0] == '#' || line[0] == 'D')
        {
            continue;
        }

        std::istringstream ss{line};
        std::string designation;
        std::string name;
        std::string ra_str;
        std::string dec_str;
        std::string mag_str;
        std::string size_str;
        std::string type_str;

        if (!std::getline(ss, designation, ',') ||
            !std::getline(ss, name, ',') ||
            !std::getline(ss, ra_str, ',') ||
            !std::getline(ss, dec_str, ',') ||
            !std::getline(ss, mag_str, ',') ||
            !std::getline(ss, size_str, ',') ||
            !std::getline(ss, type_str))
        {
            ++skipped;
            continue;
        }

        const auto ra_deg = parse_f64(trim(ra_str));
        const auto dec_deg = parse_f64(trim(dec_str));
        const auto mag = parse_f64(trim(mag_str));
        const auto size = parse_f64(trim(size_str));

        if (!ra_deg || !dec_deg || !mag || !size)
        {
            PLX_CORE_WARN("DSO line {}: parse error, skipping", line_num);
            ++skipped;
            continue;
        }

        entries.push_back(DsoEntry{
            .designation = std::string(trim(designation)),
            .common_name = std::string(trim(name)),
            .ra  = ra_deg.value() * astro_constants::kDegToRad,
            .dec = dec_deg.value() * astro_constants::kDegToRad,
            .mag_v = static_cast<f32>(mag.value()),
            .size_arcmin = static_cast<f32>(size.value()),
            .type = parse_dso_type(trim(type_str)),
        });
    }

    PLX_CORE_INFO("DSO catalog loaded: {} objects ({} skipped) from {}",
                  entries.size(), skipped, path.filename().string());

    if (entries.empty())
    {
        return std::nullopt;
    }

    return entries;
}

std::string_view DsoLoader::trim(std::string_view sv)
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

std::optional<f64> DsoLoader::parse_f64(std::string_view sv)
{
    f64 value = 0.0;
    const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    if (ec != std::errc{})
    {
        return std::nullopt;
    }
    return value;
}

} // namespace parallax::catalog
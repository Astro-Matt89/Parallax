/// @file dso_provider.cpp
/// @brief Implementation of DsoCatalogProvider.

#include "universe/dso_provider.hpp"

#include "catalog/dso_loader.hpp"
#include "core/logger.hpp"

#include <cmath>
#include <limits>
#include <numbers>

namespace parallax::universe
{

namespace
{
    constexpr double kDegreesToRadians = std::numbers::pi / 180.0;

    /// @brief Extract the Messier number from a designation string (e.g. "M1" → 1, "M31" → 31).
    /// @return Parsed number, or 0 if the string is not a valid Messier designation.
    [[nodiscard]] std::uint32_t parse_messier_number(std::string_view designation) noexcept
    {
        if (designation.size() < 2 || (designation[0] != 'M' && designation[0] != 'm'))
        {
            return 0;
        }

        std::uint32_t num = 0;
        for (std::size_t i = 1; i < designation.size(); ++i)
        {
            const char c = designation[i];
            if (c < '0' || c > '9')
            {
                return 0;
            }
            num = num * 10u + static_cast<std::uint32_t>(c - '0');
        }
        return num;
    }

    /// @brief Compute angular separation (radians) between two equatorial sky positions.
    ///
    /// Uses the Vincenty great-circle formula for numerical stability at all distances.
    /// All inputs are in radians.
    [[nodiscard]] double angular_separation(double ra1, double dec1,
                                            double ra2, double dec2) noexcept
    {
        const double dra  = ra2 - ra1;
        const double cd1  = std::cos(dec1);
        const double sd1  = std::sin(dec1);
        const double cd2  = std::cos(dec2);
        const double sd2  = std::sin(dec2);
        const double cdra = std::cos(dra);
        const double sdra = std::sin(dra);

        const double a = cd2 * sdra;
        const double b = cd1 * sd2 - sd1 * cd2 * cdra;
        const double y = std::sqrt(a * a + b * b);
        const double x = sd1 * sd2 + cd1 * cd2 * cdra;
        return std::atan2(y, x);
    }
} // namespace

// =============================================================================
// load
// =============================================================================

bool DsoCatalogProvider::load(const std::filesystem::path& messier_csv_path)
{
    auto result = catalog::DsoLoader::load_messier_csv(messier_csv_path);
    if (!result.has_value())
    {
        PLX_CORE_ERROR("DsoCatalogProvider: failed to load DSO catalog from '{}'",
                       messier_csv_path.string());
        return false;
    }

    auto raw = std::move(result.value());

    m_dsos.reserve(raw.size());
    m_messier_numbers.reserve(raw.size());
    m_messier_map.reserve(raw.size());

    for (auto& entry : raw)
    {
        const std::uint32_t num = parse_messier_number(entry.designation);
        if (num == 0)
        {
            PLX_CORE_WARN("DsoCatalogProvider: could not parse Messier number from '{}' — skipping",
                          entry.designation);
            continue;
        }

        const std::size_t idx = m_dsos.size();
        m_messier_map.emplace(num, idx);
        m_messier_numbers.push_back(num);
        m_dsos.push_back(std::move(entry));
    }

    PLX_CORE_INFO("DsoCatalogProvider: {} objects loaded ({} indexed by Messier number)",
                  m_dsos.size(), m_messier_map.size());
    return true;
}

// =============================================================================
// query_fov
// =============================================================================

void DsoCatalogProvider::query_fov(double     ra,
                                   double     dec,
                                   double     radius_deg,
                                   float      mag_limit,
                                   QueryFlags flags,
                                   std::vector<CelestialObject>& results) const
{
    if (!has_flag(flags, QueryFlags::DeepSky))
    {
        return;
    }

    const double radius_rad = radius_deg * kDegreesToRadians;

    for (std::size_t i = 0; i < m_dsos.size(); ++i)
    {
        const catalog::DsoEntry& dso = m_dsos[i];

        if (dso.mag_v > mag_limit)
        {
            continue;
        }

        if (angular_separation(ra, dec, dso.ra, dso.dec) > radius_rad)
        {
            continue;
        }

        results.push_back(make_object(dso, m_messier_numbers[i]));
    }
}

// =============================================================================
// query_object
// =============================================================================

std::optional<CelestialObject> DsoCatalogProvider::query_object(u64 id) const
{
    if (decode_type(id) != ObjectType::DeepSkyObject)
    {
        return std::nullopt;
    }

    const u64 source_id = decode_source_id(id);
    if (source_id > static_cast<u64>(std::numeric_limits<std::uint32_t>::max()))
    {
        return std::nullopt;
    }

    const auto messier_num = static_cast<std::uint32_t>(source_id);
    const auto it = m_messier_map.find(messier_num);
    if (it == m_messier_map.end())
    {
        return std::nullopt;
    }

    return make_object(m_dsos[it->second], messier_num);
}

// =============================================================================
// get_count
// =============================================================================

std::size_t DsoCatalogProvider::get_count() const
{
    return m_dsos.size();
}

// =============================================================================
// make_object (private)
// =============================================================================

CelestialObject DsoCatalogProvider::make_object(const catalog::DsoEntry& dso,
                                                 std::uint32_t messier_number) noexcept
{
    CelestialObject obj;

    obj.type     = ObjectType::DeepSkyObject;
    obj.id       = encode_id(ObjectType::DeepSkyObject, static_cast<u64>(messier_number));
    obj.ra       = dso.ra;   // radians, J2000
    obj.dec      = dso.dec;  // radians, J2000
    obj.mag_v    = dso.mag_v;
    obj.color_bv = 0.0f;     // not present in DsoEntry

    DsoData dd{};
    dd.size_arcmin = dso.size_arcmin;
    dd.dso_type    = dso.type;
    // surface_brightness, ngc_id, ic_id are not available in DsoEntry — remain zero.
    obj.data = dd;

    return obj;
}

} // namespace parallax::universe

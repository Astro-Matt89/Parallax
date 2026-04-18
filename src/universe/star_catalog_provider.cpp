/// @file star_catalog_provider.cpp
/// @brief Implementation of StarCatalogProvider.

#include "universe/star_catalog_provider.hpp"

#include "catalog/catalog_loader.hpp"
#include "core/logger.hpp"

#include <numbers>

namespace parallax::universe
{

namespace
{
    constexpr double kDegreesToRadians = std::numbers::pi / 180.0;
} // namespace

// =============================================================================
// load
// =============================================================================

bool StarCatalogProvider::load(const std::filesystem::path& tycho2_path,
                               std::optional<std::filesystem::path> hipparcos_path)
{
    // -------------------------------------------------------------------------
    // 1. Primary catalog — Tycho-2 preferred, Hipparcos fallback
    // -------------------------------------------------------------------------
    bool primary_loaded = false;

    auto tycho2 = catalog::CatalogLoader::load_tycho2_csv(tycho2_path);
    if (tycho2.has_value())
    {
        m_stars = std::move(tycho2.value());
        PLX_CORE_INFO("StarCatalogProvider: Tycho-2 loaded ({} stars)", m_stars.size());
        primary_loaded = true;
    }
    else
    {
        PLX_CORE_WARN("StarCatalogProvider: Tycho-2 not found at '{}'", tycho2_path.string());

        // Try Hipparcos as primary fallback
        if (hipparcos_path.has_value())
        {
            auto hip = catalog::CatalogLoader::load_hipparcos_csv(*hipparcos_path);
            if (hip.has_value())
            {
                m_stars = std::move(hip.value());
                PLX_CORE_INFO("StarCatalogProvider: Hipparcos loaded as primary ({} stars)",
                              m_stars.size());
                primary_loaded = true;
            }
        }
    }

    if (!primary_loaded || m_stars.empty())
    {
        PLX_CORE_ERROR("StarCatalogProvider: no primary catalog loaded — provider is empty");
        return false;
    }

    // -------------------------------------------------------------------------
    // 2. Build catalog_id → index map for O(1) query_object() lookups
    // -------------------------------------------------------------------------
    m_catalog_id_map.reserve(m_stars.size());
    for (std::size_t i = 0; i < m_stars.size(); ++i)
    {
        m_catalog_id_map.emplace(m_stars[i].catalog_id, i);
    }

    // -------------------------------------------------------------------------
    // 3. Build spatial index over the primary catalog
    // -------------------------------------------------------------------------
    PLX_CORE_INFO("StarCatalogProvider: building spatial index for {} stars...", m_stars.size());
    m_spatial_index.build(m_stars, 180);

    // -------------------------------------------------------------------------
    // 4. Hipparcos — loaded separately for constellation-line resolution
    //
    //    This is always attempted when a path is provided, even if Hipparcos
    //    was already used as the primary catalog above, so that m_hip_map is
    //    populated and resolve_hip() works regardless of which catalog is primary.
    // -------------------------------------------------------------------------
    if (hipparcos_path.has_value())
    {
        auto hip = catalog::CatalogLoader::load_hipparcos_csv(*hipparcos_path);
        if (hip.has_value())
        {
            m_hipparcos_stars = std::move(hip.value());
            PLX_CORE_INFO("StarCatalogProvider: Hipparcos loaded for constellation lookup ({} stars)",
                          m_hipparcos_stars.size());

            m_hip_map.reserve(m_hipparcos_stars.size());
            for (std::size_t i = 0; i < m_hipparcos_stars.size(); ++i)
            {
                // For Hipparcos entries catalog_id IS the HIP number.
                m_hip_map.emplace(m_hipparcos_stars[i].catalog_id, i);
            }
        }
        else
        {
            PLX_CORE_WARN("StarCatalogProvider: Hipparcos not found at '{}' — "
                          "constellation lookup will be unavailable",
                          hipparcos_path->string());
        }
    }

    return true;
}

// =============================================================================
// query_fov
// =============================================================================

void StarCatalogProvider::query_fov(double     ra,
                                    double     dec,
                                    double     radius_deg,
                                    float      mag_limit,
                                    QueryFlags flags,
                                    std::vector<CelestialObject>& results) const
{
    if (!has_flag(flags, QueryFlags::Stars))
    {
        return;
    }

    if (!m_spatial_index.is_built())
    {
        return;
    }

    const double radius_rad = radius_deg * kDegreesToRadians;
    const std::vector<u32> indices = m_spatial_index.query(ra, dec, radius_rad, mag_limit);

    results.reserve(results.size() + indices.size());
    for (const u32 idx : indices)
    {
        results.push_back(make_object(m_stars[idx]));
    }
}

// =============================================================================
// query_object
// =============================================================================

std::optional<CelestialObject> StarCatalogProvider::query_object(u64 id) const
{
    if (decode_type(id) != ObjectType::Star)
    {
        return std::nullopt;
    }

    // The lower 56 bits hold the catalog_id used when encoding this object's id.
    // For Hipparcos entries that is the HIP number; for Tycho-2 it is a u32 hash
    // of the TYC identifier string.  The lookup is symmetric: we use the same
    // catalog_id that make_object() wrote into encode_id().
    const auto source_id = static_cast<std::uint32_t>(decode_source_id(id));
    const auto it = m_catalog_id_map.find(source_id);
    if (it == m_catalog_id_map.end())
    {
        return std::nullopt;
    }

    return make_object(m_stars[it->second]);
}

// =============================================================================
// get_count
// =============================================================================

std::size_t StarCatalogProvider::get_count() const
{
    return m_stars.size();
}

// =============================================================================
// resolve_hip
// =============================================================================

std::optional<CelestialObject> StarCatalogProvider::resolve_hip(std::uint32_t hip) const
{
    const auto it = m_hip_map.find(hip);
    if (it == m_hip_map.end())
    {
        return std::nullopt;
    }

    return make_object(m_hipparcos_stars[it->second]);
}

// =============================================================================
// make_object (private)
// =============================================================================

CelestialObject StarCatalogProvider::make_object(const catalog::StarEntry& star) noexcept
{
    CelestialObject obj;

    obj.type     = ObjectType::Star;

    // ID encoding: (kStarPrefix << 56) | catalog_id.
    // For Hipparcos entries catalog_id == HIP number.
    // For Tycho-2 entries catalog_id is a u32 hash of the TYC identifier.
    // query_object() decodes symmetrically via m_catalog_id_map.
    obj.id       = encode_id(ObjectType::Star, static_cast<u64>(star.catalog_id));

    obj.ra       = star.ra;      // radians, J2000
    obj.dec      = star.dec;     // radians, J2000
    obj.mag_v    = star.mag_v;
    obj.color_bv = star.color_bv;

    // Fields not present in StarEntry (distance_pc, proper_motion_*, parallax_mas, hd_id)
    // are zero-initialised by StarData's default member initialisers.
    StarData sd{};
    // catalog_id for Hipparcos == HIP number; store it in hip_id.
    // For Tycho-2 entries this will hold the TYC hash — callers that need a true
    // HIP number should use resolve_hip() instead.
    sd.hip_id = star.catalog_id;
    obj.data  = sd;

    return obj;
}

} // namespace parallax::universe

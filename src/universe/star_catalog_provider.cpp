/// @file star_catalog_provider.cpp
/// @brief Implementation of StarCatalogProvider.

#include "universe/star_catalog_provider.hpp"

#include "catalog/catalog_loader.hpp"
#include "core/logger.hpp"

#include <algorithm>
#include <functional>
#include <numbers>
#include <string>

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
    bool hipparcos_is_primary = false;

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
                hipparcos_is_primary = true;
            }
        }
    }

    if (!primary_loaded || m_stars.empty())
    {
        PLX_CORE_ERROR("StarCatalogProvider: no primary catalog loaded — provider is empty");
        return false;
    }

    // -------------------------------------------------------------------------
    // 2. Build catalog_id → index map for O(1) query_object() lookups.
    //    StarEntry::catalog_id is u32, widened to u64 to match the map key type
    //    and to avoid narrowing when comparing against decode_source_id() results.
    // -------------------------------------------------------------------------
    m_catalog_id_map.reserve(m_stars.size());
    for (std::size_t i = 0; i < m_stars.size(); ++i)
    {
        m_catalog_id_map.emplace(static_cast<std::uint64_t>(m_stars[i].catalog_id), i);
    }

    // -------------------------------------------------------------------------
    // 3. Build spatial index over the primary catalog
    // -------------------------------------------------------------------------
    PLX_CORE_INFO("StarCatalogProvider: building spatial index for {} stars...", m_stars.size());
    m_spatial_index.build(m_stars, 180);

    // -------------------------------------------------------------------------
    // 4. Hipparcos — loaded for constellation-line resolution (resolve_hip).
    //
    //    If Hipparcos was already used as the primary catalog (step 1), reuse
    //    m_stars rather than loading the file a second time.  Otherwise load
    //    from disk so the HIP map is always populated when a path is given.
    // -------------------------------------------------------------------------
    if (hipparcos_path.has_value())
    {
        if (hipparcos_is_primary)
        {
            // Hipparcos is already in m_stars — build the HIP map directly from it
            // to avoid a redundant disk read.  We copy m_stars rather than aliasing it
            // because SpatialIndex holds a std::span over m_stars; if m_stars were ever
            // moved (e.g. during a reload), an alias in m_hipparcos_stars would dangle.
            // This trade-off is acceptable: both vectors share the same data values and
            // the Hipparcos catalog is typically ≤ 120 k entries (~4 MB).
            m_hipparcos_stars = m_stars;
            PLX_CORE_INFO("StarCatalogProvider: HIP map built from primary Hipparcos catalog ({} stars)",
                          m_hipparcos_stars.size());
        }
        else
        {
            auto hip = catalog::CatalogLoader::load_hipparcos_csv(*hipparcos_path);
            if (hip.has_value())
            {
                m_hipparcos_stars = std::move(hip.value());
                PLX_CORE_INFO("StarCatalogProvider: Hipparcos loaded for constellation lookup ({} stars)",
                              m_hipparcos_stars.size());
            }
            else
            {
                PLX_CORE_WARN("StarCatalogProvider: Hipparcos not found at '{}' — "
                              "constellation lookup will be unavailable",
                              hipparcos_path->string());
            }
        }

        m_hip_map.reserve(m_hipparcos_stars.size());
        for (std::size_t i = 0; i < m_hipparcos_stars.size(); ++i)
        {
            // For Hipparcos entries catalog_id IS the HIP number.
            m_hip_map.emplace(static_cast<std::uint64_t>(m_hipparcos_stars[i].catalog_id), i);
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
    // of the TYC identifier string.  StarEntry::catalog_id is u32, so the decoded
    // value always fits — the map key is u64 to avoid narrowing on the insert side.
    const std::uint64_t source_id = decode_source_id(id);
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
    const auto it = m_hip_map.find(static_cast<std::uint64_t>(hip));
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
    // StarData::hip_id is used to carry the catalog's native identifier:
    //   - Hipparcos entries: catalog_id == HIP number, so hip_id holds a true HIP number.
    //   - Tycho-2 entries: catalog_id is a u32 hash of the TYC string, NOT a HIP number.
    // Callers that require a verified HIP number (e.g. constellation-line resolution)
    // must use resolve_hip() instead of reading this field directly.
    sd.hip_id = star.catalog_id;
    obj.data  = sd;

    obj.is_container       = true;
    obj.parent_container_id = 0;

    // catalog_id is this provider's stable canonical identifier
    // (HIP number for Hipparcos, hashed TYC identifier for Tycho-2).
    const std::string stable_id = std::to_string(star.catalog_id);
    obj.sub_universe_seed = static_cast<u64>(std::hash<std::string>{}(stable_id));

    const double distance_pc = static_cast<double>(sd.distance_pc);
    if (distance_pc > 0.0)
    {
        const double clamped_distance_pc = std::max(distance_pc, 10.0);
        obj.containment_angular_radius_arcsec = static_cast<float>(10.0 / clamped_distance_pc);
    }
    else
    {
        // Fallback for catalog stars without a valid distance estimate.
        obj.containment_angular_radius_arcsec = 0.1f;
    }

    return obj;
}

} // namespace parallax::universe

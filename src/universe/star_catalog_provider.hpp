#pragma once

/// @file star_catalog_provider.hpp
/// @brief StarCatalogProvider — wraps the star catalog + spatial index behind DataProvider.
///
/// Owns the primary star catalog (Tycho-2 or Hipparcos fallback), its spatial index, and
/// an optional Hipparcos catalog used exclusively for constellation-line resolution.

#include "universe/data_provider.hpp"
#include "catalog/star_entry.hpp"
#include "catalog/spatial_index.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <unordered_map>
#include <vector>

namespace parallax::universe
{

/// @brief DataProvider implementation wrapping the Tycho-2/Hipparcos star catalog
///        and its declination-band spatial index.
///
/// Ownership model:
///   - @c m_stars            owns the primary catalog (Tycho-2 preferred, Hipparcos fallback).
///   - @c m_spatial_index    built from @c m_stars; valid for the lifetime of @c m_stars.
///   - @c m_catalog_id_map   maps @c StarEntry::catalog_id → index in @c m_stars (O(1) lookup
///                           in query_object()).
///   - @c m_hipparcos_stars  Hipparcos catalog loaded separately for constellation resolution.
///   - @c m_hip_map          maps HIP number → index in @c m_hipparcos_stars (O(1) lookup
///                           in resolve_hip()).
///
/// ID encoding:
///   @c encode_id(ObjectType::Star, star.catalog_id)
///   For Hipparcos entries @c catalog_id IS the HIP number.
///   For Tycho-2 entries @c catalog_id is a u32 hash of the TYC identifier string.
///   @c query_object() decodes symmetrically via @c m_catalog_id_map.
class StarCatalogProvider final : public DataProvider
{
public:
    StarCatalogProvider()                                          = default;
    ~StarCatalogProvider() override                               = default;

    StarCatalogProvider(const StarCatalogProvider&)               = delete;
    StarCatalogProvider& operator=(const StarCatalogProvider&)    = delete;

    StarCatalogProvider(StarCatalogProvider&&)                    = default;
    StarCatalogProvider& operator=(StarCatalogProvider&&)         = default;

    /// @brief Load the primary star catalog and, optionally, the Hipparcos catalog for
    ///        constellation-line resolution.
    ///
    /// Loading strategy for the primary catalog:
    ///   1. Try Tycho-2 from @p tycho2_path.
    ///   2. If that fails, try Hipparcos from @p hipparcos_path (if provided).
    ///
    /// When @p hipparcos_path is provided it is also loaded into @c m_hipparcos_stars
    /// (and the HIP map) regardless of which catalog becomes the primary, so that
    /// resolve_hip() works even when Tycho-2 is the primary.
    ///
    /// @param tycho2_path    Path to the Tycho-2 CSV file.
    /// @param hipparcos_path Optional path to the Hipparcos CSV file.
    /// @return true if at least the primary catalog was loaded successfully.
    [[nodiscard]] bool load(const std::filesystem::path& tycho2_path,
                            std::optional<std::filesystem::path> hipparcos_path = std::nullopt);

    // -------------------------------------------------------------------------
    // DataProvider overrides
    // -------------------------------------------------------------------------

    /// @brief Append all stars inside the query cone to @p results.
    ///
    /// Returns immediately (without touching @p results) if
    /// @p flags does not include QueryFlags::Stars, or if the spatial index
    /// has not been built yet.
    ///
    /// @param ra         Cone centre RA (radians, J2000).
    /// @param dec        Cone centre Dec (radians, J2000).
    /// @param radius_deg Half-angle of the query cone (degrees).
    /// @param mag_limit  Faintest magnitude to include (inclusive).
    /// @param flags      Bitmask of object categories.
    /// @param results    Output vector — objects are appended, never cleared.
    void query_fov(double     ra,
                   double     dec,
                   double     radius_deg,
                   float      mag_limit,
                   QueryFlags flags,
                   std::vector<CelestialObject>& results) const override;

    /// @brief Look up a single star by its packed u64 id.
    ///
    /// Returns @c std::nullopt if the type prefix decoded from @p id is not
    /// ObjectType::Star, or if the source id does not match any entry in the
    /// primary catalog.
    [[nodiscard]] std::optional<CelestialObject> query_object(u64 id) const override;

    /// @brief Total number of stars in the primary catalog.
    [[nodiscard]] std::size_t get_count() const override;

    // -------------------------------------------------------------------------
    // Constellation-line support
    // -------------------------------------------------------------------------

    /// @brief Resolve a HIP number to a CelestialObject for constellation-line rendering.
    ///
    /// Uses the separately-loaded Hipparcos catalog (@c m_hipparcos_stars).
    /// Returns @c std::nullopt if Hipparcos was not loaded or @p hip is not found.
    [[nodiscard]] std::optional<CelestialObject> resolve_hip(std::uint32_t hip) const;

private:
    /// @brief Convert a @c StarEntry to a @c CelestialObject.
    [[nodiscard]] static CelestialObject make_object(const catalog::StarEntry& star) noexcept;

    std::vector<catalog::StarEntry>                m_stars;           ///< Primary catalog
    catalog::SpatialIndex                          m_spatial_index;   ///< Index over m_stars
    /// catalog_id (u32 from StarEntry, stored as u64 to avoid narrowing) → index in m_stars.
    std::unordered_map<std::uint64_t, std::size_t> m_catalog_id_map;

    std::vector<catalog::StarEntry>                m_hipparcos_stars; ///< Hipparcos (constellation)
    /// HIP number (catalog_id from Hipparcos, stored as u64) → index in m_hipparcos_stars.
    std::unordered_map<std::uint64_t, std::size_t> m_hip_map;
};

} // namespace parallax::universe

#pragma once

/// @file dso_provider.hpp
/// @brief DsoCatalogProvider — wraps the Messier DSO catalog behind DataProvider.
///
/// Owns the Messier catalog (110 objects) loaded from a CSV file.  No spatial
/// index is built — 110 objects are trivially brute-forceable in query_fov().

#include "universe/data_provider.hpp"
#include "catalog/dso_entry.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <unordered_map>
#include <vector>

namespace parallax::universe
{

/// @brief DataProvider implementation wrapping the Messier DSO catalog.
///
/// Ownership model:
///   - @c m_dsos          owns the loaded catalog entries.
///   - @c m_messier_map   maps Messier number → index in @c m_dsos for O(1)
///                        query_object() lookups.
///
/// ID encoding:
///   @c encode_id(ObjectType::DeepSkyObject, messier_number)
///   @c query_object() decodes the source id as the Messier number and looks it
///   up via @c m_messier_map.
class DsoCatalogProvider final : public DataProvider
{
public:
    DsoCatalogProvider()                                           = default;
    ~DsoCatalogProvider() override                                 = default;

    DsoCatalogProvider(const DsoCatalogProvider&)                 = delete;
    DsoCatalogProvider& operator=(const DsoCatalogProvider&)      = delete;

    DsoCatalogProvider(DsoCatalogProvider&&)                      = default;
    DsoCatalogProvider& operator=(DsoCatalogProvider&&)           = default;

    /// @brief Load the Messier DSO catalog from a CSV file.
    ///
    /// @param messier_csv_path Path to the messier.csv file.
    /// @return true if the catalog was loaded successfully.
    [[nodiscard]] bool load(const std::filesystem::path& messier_csv_path);

    // -------------------------------------------------------------------------
    // DataProvider overrides
    // -------------------------------------------------------------------------

    /// @brief Append all DSOs inside the query cone to @p results.
    ///
    /// Returns immediately (without touching @p results) if
    /// @p flags does not include QueryFlags::DeepSky.
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

    /// @brief Look up a single DSO by its packed u64 id.
    ///
    /// Returns @c std::nullopt if the type prefix decoded from @p id is not
    /// ObjectType::DeepSkyObject, or if the Messier number is not found.
    [[nodiscard]] std::optional<CelestialObject> query_object(u64 id) const override;

    /// @brief Total number of DSOs in the loaded catalog.
    [[nodiscard]] std::size_t get_count() const override;

private:
    /// @brief Convert a @c DsoEntry to a @c CelestialObject.
    [[nodiscard]] static CelestialObject make_object(const catalog::DsoEntry& dso,
                                                     std::uint32_t messier_number) noexcept;

    std::vector<catalog::DsoEntry>                 m_dsos;            ///< Loaded catalog (valid Messier entries only)
    std::vector<std::uint32_t>                     m_messier_numbers; ///< Parallel to m_dsos; Messier number per entry
    /// Messier number → index in m_dsos.
    std::unordered_map<std::uint32_t, std::size_t> m_messier_map;
};

} // namespace parallax::universe

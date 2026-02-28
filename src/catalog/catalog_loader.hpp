#pragma once

/// @file catalog_loader.hpp
/// @brief Loads star catalogs from CSV/text files into memory.

#include "catalog/star_entry.hpp"
#include "core/types.hpp"

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace parallax::catalog
{
    /// @brief Static utility class for loading star catalog files.
    ///
    /// Phase 1 supports CSV loading for Hipparcos-style and bright-star catalogs.
    /// Binary .plxcat format will be added in a later task.
    class CatalogLoader
    {
    public:
        CatalogLoader() = delete;

        /// @brief Load stars from a bright-star CSV file.
        ///
        /// Expected CSV columns (header row required):
        ///   Name, RA_deg, Dec_deg, Vmag, BV
        ///
        /// RA and Dec are in degrees and will be converted to radians.
        /// The Name column is read but not stored in StarEntry.
        /// catalog_id is assigned as the 1-based line index.
        ///
        /// @param path Path to the CSV file.
        /// @return Vector of StarEntry on success, std::nullopt on failure.
        [[nodiscard]] static std::optional<std::vector<StarEntry>>
            load_bright_star_csv(const std::filesystem::path& path);

        /// @brief Load stars from a Hipparcos-format CSV file.
        ///
        /// Expected CSV columns (header row required):
        ///   HIP, RA_deg, Dec_deg, Vmag, BV
        ///
        /// RA and Dec are in degrees and will be converted to radians.
        /// catalog_id is set to the HIP number.
        ///
        /// @param path Path to the CSV file.
        /// @return Vector of StarEntry on success, std::nullopt on failure.
        [[nodiscard]] static std::optional<std::vector<StarEntry>>
            load_hipparcos_csv(const std::filesystem::path& path);

    private:
        /// @brief Trim leading and trailing whitespace from a string_view.
        [[nodiscard]] static std::string_view trim(std::string_view sv);

        /// @brief Parse a single f64 value from a trimmed string_view.
        /// @return The parsed value, or std::nullopt on failure.
        [[nodiscard]] static std::optional<f64> parse_f64(std::string_view sv);

        /// @brief Parse a single u32 value from a trimmed string_view.
        /// @return The parsed value, or std::nullopt on failure.
        [[nodiscard]] static std::optional<u32> parse_u32(std::string_view sv);
    };

} // namespace parallax::catalog
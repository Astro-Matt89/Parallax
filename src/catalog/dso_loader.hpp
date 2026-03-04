#pragma once

/// @file dso_loader.hpp
/// @brief Loads deep sky object catalogs from CSV files.

#include "catalog/dso_entry.hpp"
#include "core/types.hpp"

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace parallax::catalog
{
    /// @brief Static utility class for loading DSO catalog files.
    class DsoLoader
    {
    public:
        DsoLoader() = delete;

        /// @brief Load DSOs from a Messier-format CSV file.
        ///
        /// Expected CSV columns (header row required):
        ///   Designation,Name,RA_deg,Dec_deg,Vmag,Size_arcmin,Type
        ///
        /// RA and Dec are in degrees and will be converted to radians.
        ///
        /// @param path Path to the CSV file.
        /// @return Vector of DsoEntry on success, std::nullopt on failure.
        [[nodiscard]] static std::optional<std::vector<DsoEntry>>
            load_messier_csv(const std::filesystem::path& path);

    private:
        [[nodiscard]] static std::string_view trim(std::string_view sv);
        [[nodiscard]] static std::optional<f64> parse_f64(std::string_view sv);
    };

} // namespace parallax::catalog
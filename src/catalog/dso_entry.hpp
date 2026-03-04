#pragma once

/// @file dso_entry.hpp
/// @brief Deep sky object data structure and type classification.

#include "core/types.hpp"

#include <string>

namespace parallax::catalog
{
    /// @brief Classification of deep sky object morphological types.
    enum class DsoType : u8
    {
        Galaxy,
        Nebula,             ///< Emission, reflection, planetary
        OpenCluster,
        GlobularCluster,
        SupernovaRemnant,
        Other
    };

    /// @brief Runtime representation of a single deep sky object.
    ///
    /// Coordinates stored in radians (J2000 epoch), same as StarEntry.
    struct DsoEntry
    {
        std::string designation;    ///< "M1", "M31", "M42"
        std::string common_name;    ///< "Crab Nebula", "Andromeda Galaxy"
        f64 ra;                     ///< Right ascension (radians, 0..2π)
        f64 dec;                    ///< Declination (radians, -π/2..+π/2)
        f32 mag_v;                  ///< Visual magnitude
        f32 size_arcmin;            ///< Apparent size, major axis (arcminutes)
        DsoType type;               ///< Morphological classification
    };

    /// @brief Convert a DSO type string from CSV to enum.
    /// @param s Type string: "Galaxy", "Nebula", "OpenCluster", etc.
    /// @return Parsed type, or DsoType::Other if unrecognized.
    [[nodiscard]] inline DsoType parse_dso_type(std::string_view s)
    {
        if (s == "Galaxy")            return DsoType::Galaxy;
        if (s == "Nebula")            return DsoType::Nebula;
        if (s == "OpenCluster")       return DsoType::OpenCluster;
        if (s == "GlobularCluster")   return DsoType::GlobularCluster;
        if (s == "SupernovaRemnant")  return DsoType::SupernovaRemnant;
        return DsoType::Other;
    }

    /// @brief Human-readable name for a DSO type (for HUD / debug).
    [[nodiscard]] inline const char* dso_type_name(DsoType type)
    {
        switch (type)
        {
            case DsoType::Galaxy:           return "Galaxy";
            case DsoType::Nebula:           return "Nebula";
            case DsoType::OpenCluster:      return "Open Cluster";
            case DsoType::GlobularCluster:  return "Globular Cluster";
            case DsoType::SupernovaRemnant: return "SNR";
            case DsoType::Other:            return "Other";
        }
        return "???";
    }

} // namespace parallax::catalog
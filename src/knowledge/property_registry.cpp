/// @file property_registry.cpp
/// @brief PropertyRegistry — static property tables for every object type.

#include "knowledge/property_registry.hpp"

#include <algorithm>
#include <vector>

namespace parallax::knowledge
{

// =============================================================================
// Internal helpers — function-local static tables
// =============================================================================
//
// Each helper returns a span over a function-local static vector, constructed
// once on first call.  This avoids static-initialisation-order issues while
// keeping all property data in a single source-of-truth location.
//
// Knowledge-level mapping (problem statement → KnowledgeLevel enum):
//   L1 → Detected        (SNR  5-10,   1-4  h)
//   L2 → Classified      (SNR 20-30,   2-8  h)
//   L3 → Characterized   (SNR 50-100,  4-16 h)
//   L4 → Detailed        (SNR 150-250, 8-24 h)
//   L5 → Resolved        (SNR 300+,   16-48 h)
//   L6 → FullyMapped     (SNR 500+,   24-72 h)
// =============================================================================

namespace
{

// Convenience aliases
using KL = KnowledgeLevel;
using MT = MeasurementTechnique;

// -----------------------------------------------------------------------------
// Star (and ProceduralStar share the same descriptor set)
// -----------------------------------------------------------------------------
std::span<const PropertyDescriptor> star_table()
{
    static const std::vector<PropertyDescriptor> kTable =
    {
        // L1 — Detected
        { "ra",                    KL::Detected,      MT::Astrometry,            8.0f,   2.0f  },
        { "dec",                   KL::Detected,      MT::Astrometry,            8.0f,   2.0f  },
        { "mag_v",                 KL::Detected,      MT::BroadbandPhotometry,   8.0f,   2.0f  },

        // L2 — Classified
        { "color_bv",              KL::Classified,    MT::BroadbandPhotometry,   25.0f,  4.0f  },
        { "spectral_type",         KL::Classified,    MT::SpectroscopyLowRes,    25.0f,  5.0f  },

        // L3 — Characterized
        { "parallax_mas",          KL::Characterized, MT::Astrometry,            75.0f,  8.0f  },
        { "distance_pc",           KL::Characterized, MT::Astrometry,            75.0f,  8.0f  },
        { "radial_velocity_kms",   KL::Characterized, MT::RadialVelocity,        90.0f, 10.0f  },
        { "detailed_spectrum",     KL::Characterized, MT::SpectroscopyHighRes,  100.0f, 14.0f  },

        // L4 — Detailed
        { "rotation_velocity_kms", KL::Detailed,      MT::SpectroscopyHighRes,  180.0f, 14.0f  },
        { "magnetic_field_gauss",  KL::Detailed,      MT::SpectroscopyHighRes,  250.0f, 24.0f  },
        { "metallicity",           KL::Detailed,      MT::SpectroscopyHighRes,  200.0f, 16.0f  },

        // L5 — Resolved
        { "sub_universe_unlocked", KL::Resolved,      MT::Interferometry,       350.0f, 32.0f  },

        // L6 — FullyMapped
        { "activity_cycle_years",  KL::FullyMapped,   MT::PrecisionPhotometry,  550.0f, 48.0f  },
        { "starspot_fraction",     KL::FullyMapped,   MT::PrecisionPhotometry,  700.0f, 60.0f  },
    };
    return kTable;
}

// -----------------------------------------------------------------------------
// SolarSystemBody
// -----------------------------------------------------------------------------
std::span<const PropertyDescriptor> solar_system_body_table()
{
    static const std::vector<PropertyDescriptor> kTable =
    {
        // L1 — Detected
        { "ra",                        KL::Detected,      MT::Astrometry,           8.0f,   1.0f  },
        { "dec",                       KL::Detected,      MT::Astrometry,           8.0f,   1.0f  },
        { "mag_v",                     KL::Detected,      MT::BroadbandPhotometry,  8.0f,   1.0f  },

        // L2 — Classified
        { "distance_au",               KL::Classified,    MT::Astrometry,           20.0f,  2.0f  },
        { "angular_diameter_arcsec",   KL::Classified,    MT::Astrometry,           20.0f,  2.0f  },
        { "phase_illumination",        KL::Classified,    MT::BroadbandPhotometry,  25.0f,  4.0f  },

        // L3 — Characterized
        { "spectral_albedo",           KL::Characterized, MT::BroadbandPhotometry,  75.0f,  8.0f  },
        { "rotation_period_hours",     KL::Characterized, MT::PrecisionPhotometry,  90.0f, 10.0f  },

        // L4 — Detailed
        { "surface_composition",       KL::Detailed,      MT::SpectroscopyLowRes,  180.0f, 14.0f  },
        { "atmosphere_pressure",       KL::Detailed,      MT::SpectroscopyLowRes,  200.0f, 18.0f  },

        // L5 — Resolved
        { "sub_universe_unlocked",     KL::Resolved,      MT::Interferometry,      350.0f, 32.0f  },

        // L6 — FullyMapped
        { "detailed_surface_map",      KL::FullyMapped,   MT::Interferometry,      600.0f, 48.0f  },
    };
    return kTable;
}

// -----------------------------------------------------------------------------
// DeepSkyObject / Galaxy (share the same descriptor set)
// Covers galaxies, nebulae, clusters.
// -----------------------------------------------------------------------------
std::span<const PropertyDescriptor> dso_table()
{
    static const std::vector<PropertyDescriptor> kTable =
    {
        // L1 — Detected
        { "ra",                  KL::Detected,      MT::Astrometry,           8.0f,   2.0f  },
        { "dec",                 KL::Detected,      MT::Astrometry,           8.0f,   2.0f  },
        { "mag_v",               KL::Detected,      MT::BroadbandPhotometry,  8.0f,   2.0f  },
        { "size_arcmin",         KL::Detected,      MT::Astrometry,           8.0f,   2.0f  },

        // L2 — Classified
        { "hubble_type",         KL::Classified,    MT::SpectroscopyLowRes,   25.0f,  4.0f  },
        { "nebula_subtype",      KL::Classified,    MT::SpectroscopyLowRes,   25.0f,  4.0f  },

        // L3 — Characterized
        { "redshift",            KL::Characterized, MT::SpectroscopyLowRes,   75.0f,  8.0f  },
        { "distance_mpc",        KL::Characterized, MT::SpectroscopyHighRes,  90.0f, 12.0f  },
        { "integrated_spectrum", KL::Characterized, MT::SpectroscopyHighRes, 100.0f, 16.0f  },

        // L4 — Detailed
        { "velocity_dispersion", KL::Detailed,      MT::SpectroscopyHighRes, 200.0f, 16.0f  },
        { "stellar_populations", KL::Detailed,      MT::SpectroscopyHighRes, 250.0f, 24.0f  },

        // L5 — Resolved
        { "sub_universe_unlocked", KL::Resolved,    MT::Interferometry,      350.0f, 40.0f  },

        // L6 — FullyMapped
        { "dark_matter_profile", KL::FullyMapped,   MT::RadioObservation,    550.0f, 48.0f  },
        { "dynamical_mass",      KL::FullyMapped,   MT::SpectroscopyHighRes, 700.0f, 64.0f  },
    };
    return kTable;
}

} // anonymous namespace

// -----------------------------------------------------------------------------
// Exoplanet — property set defined for future use.
//
// ObjectType::Exoplanet does not yet exist in universe/object_id.hpp (planned
// for Sprint 09+).  When it is added, insert the case in get_properties() and
// route it to the table below.
//
// L1: transit_detected (PrecisionPhotometry, SNR 8, 3h),
//     host_star_id (Astrometry, SNR 8, 1h)
// L2: period_days (PrecisionPhotometry, SNR 25, 6h),
//     transit_depth (PrecisionPhotometry, SNR 25, 6h),
//     transit_duration (PrecisionPhotometry, SNR 20, 4h)
// L3: radius_earth (PrecisionPhotometry, SNR 75, 8h),
//     mass_earth (RadialVelocity, SNR 90, 12h),
//     semi_major_axis_au (Astrometry, SNR 75, 8h),
//     eccentricity (RadialVelocity, SNR 100, 16h)
// L4: atmosphere_composition (SpectroscopyLowRes, SNR 200, 16h),
//     temperature_k (SpectroscopyLowRes, SNR 180, 14h)
// L5: direct_image (Coronagraphy, SNR 350, 40h),
//     albedo_color (Coronagraphy, SNR 350, 36h),
//     phase_variation (PrecisionPhotometry, SNR 320, 32h)
// L6: surface_map (Coronagraphy, SNR 600, 56h),
//     rotation_period_hours (PrecisionPhotometry, SNR 550, 48h)
// -----------------------------------------------------------------------------

// =============================================================================
// PropertyRegistry — public API
// =============================================================================

std::span<const PropertyDescriptor>
PropertyRegistry::get_properties(universe::ObjectType type)
{
    using OT = universe::ObjectType;

    switch (type)
    {
        case OT::Star:
        case OT::ProceduralStar:
            return star_table();

        case OT::SolarSystemBody:
            return solar_system_body_table();

        case OT::DeepSkyObject:
        case OT::Galaxy:
        case OT::ProceduralDso:
            return dso_table();

        default:
            return {};
    }
}

std::optional<PropertyDescriptor>
PropertyRegistry::get_property(universe::ObjectType type, std::string_view name)
{
    const auto all = get_properties(type);
    const auto it  = std::find_if(all.begin(), all.end(),
        [name](const PropertyDescriptor& pd) { return pd.name == name; });

    if (it == all.end())
    {
        return std::nullopt;
    }
    return *it;
}

std::vector<PropertyDescriptor>
PropertyRegistry::get_properties_for_level(universe::ObjectType type, KnowledgeLevel level)
{
    const auto all = get_properties(type);
    std::vector<PropertyDescriptor> result;

    for (const auto& pd : all)
    {
        if (pd.unlocks_at == level)
        {
            result.push_back(pd);
        }
    }

    return result;
}

} // namespace parallax::knowledge

/// @file knowledge_database_init.cpp
/// @brief KnowledgeDatabase::initialize_from_historical_catalogs implementation.
///
/// Separated from knowledge_database.cpp so that the core database logic can
/// be linked by unit tests without pulling in the full Universe Engine stack.

#include "knowledge/knowledge_database.hpp"

#include "universe/celestial_object.hpp"
#include "universe/data_provider.hpp"
#include "universe/universe.hpp"

#include <variant>
#include <vector>

namespace parallax::knowledge
{

namespace
{

/// Session ID reserved for historical catalog measurements.
constexpr std::uint64_t kHistoricalSessionId   = 0;

/// SNR sentinel indicating catalog-grade quality data.
constexpr float kHistoricalSnr = 1000.0f;

/// Uncertainty for catalog-grade measurements (effectively noise-free).
constexpr float kHistoricalUncertainty = 0.0f;

} // anonymous namespace

void KnowledgeDatabase::initialize_from_historical_catalogs(
    const universe::Universe& universe)
{
    using namespace universe;

    // Full-sky cone: RA=0, Dec=0, radius=180 degrees captures all objects.
    // Exclude procedural objects — only real catalog entries are pre-populated.
    constexpr double kFullSkyRa     = 0.0;
    constexpr double kFullSkyDec    = 0.0;
    constexpr double kFullSkyRadius = 180.0;
    constexpr float  kMagLimit      = 99.0f;

    const QueryFlags kRealFlags = QueryFlags::Stars
                                | QueryFlags::SolarSystem
                                | QueryFlags::DeepSky;

    std::vector<CelestialObject> objects;
    objects.reserve(4096);
    universe.query_fov(kFullSkyRa, kFullSkyDec, kFullSkyRadius,
                       kMagLimit, kRealFlags, objects);

    for (const auto& obj : objects)
    {
        if (!obj.is_real())
        {
            continue;
        }

        ObjectKnowledge& ok  = ensure_entry(obj.id);
        ok.object_id         = obj.id;
        ok.current_level     = kHistoricalBaselineLevel; // L3: Characterized
        ok.is_historical     = true;
        ok.is_confirmed      = true;
        ok.independent_detections = 0; // historical — not player-detected

        // Helper: add a single measurement for a named property.
        auto add_meas = [&](std::string_view name, double value)
        {
            MeasurementRecord rec;
            rec.value          = value;
            rec.uncertainty    = kHistoricalUncertainty;
            rec.snr            = kHistoricalSnr;
            rec.session_id     = kHistoricalSessionId;
            rec.observation_jd = 0.0; // catalog-grade — no specific JD
            ok.measurements.emplace(std::string(name), rec);
        };

        // Position and magnitude are present on every real CelestialObject.
        add_meas("ra",    obj.ra);
        add_meas("dec",   obj.dec);
        add_meas("mag_v", static_cast<double>(obj.mag_v));

        // Type-specific measurements — only extract fields that actually exist.
        switch (obj.type)
        {
            case ObjectType::Star:
            {
                // color_bv is a common field on CelestialObject base struct.
                add_meas("color_bv", static_cast<double>(obj.color_bv));

                if (const auto* sd = std::get_if<StarData>(&obj.data))
                {
                    if (sd->parallax_mas > 0.0f)
                    {
                        add_meas("parallax_mas",
                                 static_cast<double>(sd->parallax_mas));
                    }
                    if (sd->distance_pc > 0.0f)
                    {
                        add_meas("distance_pc",
                                 static_cast<double>(sd->distance_pc));
                    }
                }
                break;
            }

            case ObjectType::SolarSystemBody:
            {
                if (const auto* ss = std::get_if<SolarSystemData>(&obj.data))
                {
                    if (ss->distance_au > 0.0f)
                    {
                        add_meas("distance_au",
                                 static_cast<double>(ss->distance_au));
                    }
                    if (ss->apparent_diameter_arcsec > 0.0f)
                    {
                        add_meas("angular_diameter_arcsec",
                                 static_cast<double>(ss->apparent_diameter_arcsec));
                    }
                }
                break;
            }

            case ObjectType::DeepSkyObject:
            case ObjectType::Galaxy:
            {
                if (const auto* dd = std::get_if<DsoData>(&obj.data))
                {
                    if (dd->size_arcmin > 0.0f)
                    {
                        add_meas("size_arcmin",
                                 static_cast<double>(dd->size_arcmin));
                    }
                    // Serialize the DsoType enum as a plain integer.
                    add_meas("subtype",
                             static_cast<double>(static_cast<int>(dd->dso_type)));
                }
                break;
            }

            default:
                break;
        }
    }
}

} // namespace parallax::knowledge

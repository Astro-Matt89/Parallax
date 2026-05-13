/// @file mock_analyzer.cpp
/// @brief MockAnalyzer implementation.

#include "analysis/mock_analyzer.hpp"

#include <cmath>
#include <string_view>
#include <variant>

#include "knowledge/property_registry.hpp"
#include "observation/data_record.hpp"
#include "universe/celestial_object.hpp"
#include "universe/universe.hpp"

namespace parallax::analysis
{

namespace
{
constexpr double kSnrThresholdL1 = 5.0;
constexpr double kSnrThresholdL2 = 20.0;
constexpr double kSnrThresholdL3 = 50.0;
constexpr double kParallaxToDistancePcFactor = 1000.0;

constexpr double kBaseUncertaintyRaDec  = 1.0e-3;
constexpr double kBaseUncertaintyMag    = 1.0e-2;
constexpr double kBaseUncertaintyColor  = 1.0e-2;
constexpr double kBaseUncertaintyParallax = 1.0;
constexpr double kBaseUncertaintyDistance = 0.5;

[[nodiscard]] double compute_mock_uncertainty(double base, double snr)
{
    // MOCK placeholder formula. Sprint 09+ will use physically-derived
    // per-technique error models.
    if (snr <= 0.0)
    {
        return base;
    }

    return base / std::sqrt(snr);
}

[[nodiscard]] bool property_exists(universe::ObjectType type, std::string_view name)
{
    return knowledge::PropertyRegistry::get_property(type, name).has_value();
}

} // namespace

std::vector<KnowledgeUpdate>
MockAnalyzer::analyze(const observation::DataRecord& record,
                      const universe::Universe&      universe) const
{
    if (record.target_object_id == 0)
    {
        return {};
    }

    const std::optional<universe::CelestialObject> object_opt =
        universe.query_object(record.target_object_id);
    if (!object_opt.has_value())
    {
        return {};
    }

    const universe::CelestialObject& object = *object_opt;
    const bool is_star_type = object.type == universe::ObjectType::Star
                           || object.type == universe::ObjectType::ProceduralStar;
    if (!is_star_type)
    {
        // TODO(Sprint 09): Add SolarSystemBody and DeepSkyObject measurement mapping.
        return {};
    }

    const auto* star_data = std::get_if<universe::StarData>(&object.data);
    if (star_data == nullptr)
    {
        return {};
    }

    const double snr = record.achieved_snr;
    std::vector<KnowledgeUpdate> updates;

    auto add_update = [&](std::string_view property_name,
                          MeasurementValue value,
                          double           base_uncertainty)
    {
        if (!property_exists(object.type, property_name))
        {
            return;
        }

        updates.push_back(KnowledgeUpdate{
            .object_id    = object.id,
            .property_name = std::string(property_name),
            .value        = value,
            .uncertainty  = compute_mock_uncertainty(base_uncertainty, snr),
            .snr          = snr,
            .new_level    = std::nullopt,
        });
    };

    if (snr >= kSnrThresholdL1)
    {
        add_update("ra", object.ra, kBaseUncertaintyRaDec);
        add_update("dec", object.dec, kBaseUncertaintyRaDec);
        add_update("mag_v", static_cast<double>(object.mag_v), kBaseUncertaintyMag);
    }

    if (snr >= kSnrThresholdL2)
    {
        add_update("color_bv", static_cast<double>(object.color_bv), kBaseUncertaintyColor);

        // `spectral_type` exists in PropertyRegistry but no corresponding field
        // currently exists on CelestialObject/StarData, so no update is emitted.
    }

    if (snr >= kSnrThresholdL3)
    {
        if (star_data->parallax_mas > 0.0f)
        {
            const double parallax_mas = static_cast<double>(star_data->parallax_mas);
            add_update("parallax_mas", parallax_mas, kBaseUncertaintyParallax);

            if (star_data->distance_pc > 0.0f)
            {
                add_update("distance_pc",
                           static_cast<double>(star_data->distance_pc),
                           kBaseUncertaintyDistance);
            }
            else
            {
                add_update("distance_pc",
                           kParallaxToDistancePcFactor / parallax_mas,
                           kBaseUncertaintyDistance);
            }
        }
        else if (star_data->distance_pc > 0.0f)
        {
            add_update("distance_pc",
                       static_cast<double>(star_data->distance_pc),
                       kBaseUncertaintyDistance);
        }
    }

    return updates;
}

} // namespace parallax::analysis

/// @file test_property_registry.cpp
/// @brief Unit tests for Task 8.2: PropertyRegistry.
///
/// Verifies that PropertyRegistry correctly returns property tables for every
/// ObjectType, filters by KnowledgeLevel, and looks up properties by name.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "knowledge/property_registry.hpp"
#include "universe/object_id.hpp"

using namespace parallax::knowledge;
using namespace parallax::universe;

// =============================================================================
// Helpers
// =============================================================================

static bool contains_name(std::span<const PropertyDescriptor> props, const char* name)
{
    for (const auto& p : props)
    {
        if (p.name == name)
        {
            return true;
        }
    }
    return false;
}

// =============================================================================
// Star
// =============================================================================

TEST_CASE("Star: get_properties returns non-empty table")
{
    const auto props = PropertyRegistry::get_properties(ObjectType::Star);
    CHECK_FALSE(props.empty());
}

TEST_CASE("Star: L1 properties present")
{
    const auto props = PropertyRegistry::get_properties(ObjectType::Star);
    CHECK(contains_name(props, "ra"));
    CHECK(contains_name(props, "dec"));
    CHECK(contains_name(props, "mag_v"));
}

TEST_CASE("Star: L2 properties present")
{
    const auto props = PropertyRegistry::get_properties(ObjectType::Star);
    CHECK(contains_name(props, "color_bv"));
    CHECK(contains_name(props, "spectral_type"));
}

TEST_CASE("Star: L3 properties present")
{
    const auto props = PropertyRegistry::get_properties(ObjectType::Star);
    CHECK(contains_name(props, "parallax_mas"));
    CHECK(contains_name(props, "distance_pc"));
    CHECK(contains_name(props, "radial_velocity_kms"));
    CHECK(contains_name(props, "detailed_spectrum"));
}

TEST_CASE("Star: L4 properties present")
{
    const auto props = PropertyRegistry::get_properties(ObjectType::Star);
    CHECK(contains_name(props, "rotation_velocity_kms"));
    CHECK(contains_name(props, "magnetic_field_gauss"));
    CHECK(contains_name(props, "metallicity"));
}

TEST_CASE("Star: L5 sub_universe_unlocked present")
{
    const auto props = PropertyRegistry::get_properties(ObjectType::Star);
    CHECK(contains_name(props, "sub_universe_unlocked"));
}

TEST_CASE("Star: L6 properties present")
{
    const auto props = PropertyRegistry::get_properties(ObjectType::Star);
    CHECK(contains_name(props, "activity_cycle_years"));
    CHECK(contains_name(props, "starspot_fraction"));
}

TEST_CASE("Star: get_property returns correct descriptor for mag_v")
{
    const auto opt = PropertyRegistry::get_property(ObjectType::Star, "mag_v");
    REQUIRE(opt.has_value());
    CHECK(opt->name        == "mag_v");
    CHECK(opt->unlocks_at  == KnowledgeLevel::Detected);
}

TEST_CASE("Star: get_property returns nullopt for unknown name")
{
    const auto opt = PropertyRegistry::get_property(ObjectType::Star, "nonexistent_property");
    CHECK_FALSE(opt.has_value());
}

TEST_CASE("Star: get_properties_for_level returns only L1 (Detected) descriptors")
{
    const auto l1 = PropertyRegistry::get_properties_for_level(
        ObjectType::Star, KnowledgeLevel::Detected);
    CHECK_FALSE(l1.empty());
    for (const auto& pd : l1)
    {
        CHECK(pd.unlocks_at == KnowledgeLevel::Detected);
    }
    CHECK(l1.size() == 3u);   // ra, dec, mag_v
}

TEST_CASE("Star: SNR values are in valid ranges per level")
{
    const auto all = PropertyRegistry::get_properties(ObjectType::Star);
    for (const auto& pd : all)
    {
        switch (pd.unlocks_at)
        {
            case KnowledgeLevel::Detected:
                CHECK(pd.required_snr >= 5.0f);
                CHECK(pd.required_snr <= 10.0f);
                break;
            case KnowledgeLevel::Classified:
                CHECK(pd.required_snr >= 20.0f);
                CHECK(pd.required_snr <= 30.0f);
                break;
            case KnowledgeLevel::Characterized:
                CHECK(pd.required_snr >= 50.0f);
                CHECK(pd.required_snr <= 100.0f);
                break;
            case KnowledgeLevel::Detailed:
                CHECK(pd.required_snr >= 150.0f);
                CHECK(pd.required_snr <= 250.0f);
                break;
            case KnowledgeLevel::Resolved:
                CHECK(pd.required_snr >= 300.0f);
                break;
            case KnowledgeLevel::FullyMapped:
                CHECK(pd.required_snr >= 500.0f);
                break;
            default:
                break;
        }
    }
}

// =============================================================================
// ProceduralStar — same descriptor set as Star
// =============================================================================

TEST_CASE("ProceduralStar: shares Star property table")
{
    const auto star_props  = PropertyRegistry::get_properties(ObjectType::Star);
    const auto proc_props  = PropertyRegistry::get_properties(ObjectType::ProceduralStar);
    CHECK(star_props.size() == proc_props.size());
    CHECK(contains_name(proc_props, "ra"));
    CHECK(contains_name(proc_props, "mag_v"));
    CHECK(contains_name(proc_props, "metallicity"));
    CHECK(contains_name(proc_props, "sub_universe_unlocked"));
}

// =============================================================================
// SolarSystemBody
// =============================================================================

TEST_CASE("SolarSystemBody: get_properties returns non-empty table")
{
    const auto props = PropertyRegistry::get_properties(ObjectType::SolarSystemBody);
    CHECK_FALSE(props.empty());
}

TEST_CASE("SolarSystemBody: all required properties present")
{
    const auto props = PropertyRegistry::get_properties(ObjectType::SolarSystemBody);
    CHECK(contains_name(props, "ra"));
    CHECK(contains_name(props, "dec"));
    CHECK(contains_name(props, "mag_v"));
    CHECK(contains_name(props, "distance_au"));
    CHECK(contains_name(props, "angular_diameter_arcsec"));
    CHECK(contains_name(props, "phase_illumination"));
    CHECK(contains_name(props, "spectral_albedo"));
    CHECK(contains_name(props, "rotation_period_hours"));
    CHECK(contains_name(props, "surface_composition"));
    CHECK(contains_name(props, "atmosphere_pressure"));
    CHECK(contains_name(props, "sub_universe_unlocked"));
    CHECK(contains_name(props, "detailed_surface_map"));
}

// =============================================================================
// DeepSkyObject / Galaxy
// =============================================================================

TEST_CASE("DeepSkyObject: get_properties returns non-empty table")
{
    const auto props = PropertyRegistry::get_properties(ObjectType::DeepSkyObject);
    CHECK_FALSE(props.empty());
}

TEST_CASE("DeepSkyObject: all required properties present")
{
    const auto props = PropertyRegistry::get_properties(ObjectType::DeepSkyObject);
    CHECK(contains_name(props, "ra"));
    CHECK(contains_name(props, "dec"));
    CHECK(contains_name(props, "mag_v"));
    CHECK(contains_name(props, "size_arcmin"));
    CHECK(contains_name(props, "hubble_type"));
    CHECK(contains_name(props, "nebula_subtype"));
    CHECK(contains_name(props, "redshift"));
    CHECK(contains_name(props, "distance_mpc"));
    CHECK(contains_name(props, "integrated_spectrum"));
    CHECK(contains_name(props, "velocity_dispersion"));
    CHECK(contains_name(props, "stellar_populations"));
    CHECK(contains_name(props, "sub_universe_unlocked"));
    CHECK(contains_name(props, "dark_matter_profile"));
    CHECK(contains_name(props, "dynamical_mass"));
}

TEST_CASE("Galaxy: shares DeepSkyObject property table")
{
    const auto dso  = PropertyRegistry::get_properties(ObjectType::DeepSkyObject);
    const auto gal  = PropertyRegistry::get_properties(ObjectType::Galaxy);
    CHECK(dso.size() == gal.size());
}

TEST_CASE("ProceduralDso: shares DeepSkyObject property table")
{
    const auto dso  = PropertyRegistry::get_properties(ObjectType::DeepSkyObject);
    const auto proc = PropertyRegistry::get_properties(ObjectType::ProceduralDso);
    CHECK(dso.size() == proc.size());
}

// =============================================================================
// Unknown type — empty span
// =============================================================================

TEST_CASE("Unknown ObjectType returns empty span")
{
    const auto props = PropertyRegistry::get_properties(ObjectType::Unknown);
    CHECK(props.empty());
}

// =============================================================================
// get_properties_for_level — cross-type
// =============================================================================

TEST_CASE("get_properties_for_level: results contain only the requested level")
{
    for (const auto type : { ObjectType::Star, ObjectType::SolarSystemBody,
                             ObjectType::DeepSkyObject, ObjectType::Galaxy,
                             ObjectType::ProceduralStar, ObjectType::ProceduralDso })
    {
        for (const auto level : { KnowledgeLevel::Detected,
                                  KnowledgeLevel::Classified,
                                  KnowledgeLevel::Characterized,
                                  KnowledgeLevel::Detailed,
                                  KnowledgeLevel::Resolved,
                                  KnowledgeLevel::FullyMapped })
        {
            const auto filtered = PropertyRegistry::get_properties_for_level(type, level);
            for (const auto& pd : filtered)
            {
                CHECK(pd.unlocks_at == level);
            }
        }
    }
}

/// @file test_knowledge_core_types.cpp
/// @brief Unit tests for Task 8.1: Knowledge Core Types.
///
/// Verifies KnowledgeLevel, MeasurementTechnique, PropertyDescriptor,
/// and MeasurementRecord compile correctly and hold expected values.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "knowledge/knowledge_level.hpp"
#include "knowledge/measurement_technique.hpp"
#include "knowledge/property_descriptor.hpp"
#include "knowledge/measurement_record.hpp"

#include "core/types.hpp"

using namespace parallax;
using namespace parallax::knowledge;

// =================================================================
// KnowledgeLevel
// =================================================================

TEST_CASE("KnowledgeLevel values match specification")
{
    CHECK(static_cast<u8>(KnowledgeLevel::Unknown)       == 0);
    CHECK(static_cast<u8>(KnowledgeLevel::Detected)      == 1);
    CHECK(static_cast<u8>(KnowledgeLevel::Classified)    == 2);
    CHECK(static_cast<u8>(KnowledgeLevel::Characterized) == 3);
    CHECK(static_cast<u8>(KnowledgeLevel::Detailed)      == 4);
    CHECK(static_cast<u8>(KnowledgeLevel::Resolved)      == 5);
    CHECK(static_cast<u8>(KnowledgeLevel::FullyMapped)   == 6);
    CHECK(static_cast<u8>(KnowledgeLevel::Reserved)      == 7);
}

TEST_CASE("kHistoricalBaselineLevel is Characterized (L3)")
{
    CHECK(kHistoricalBaselineLevel == KnowledgeLevel::Characterized);
}

// =================================================================
// MeasurementTechnique
// =================================================================

TEST_CASE("MeasurementTechnique: None is the first enum value")
{
    CHECK(static_cast<u16>(MeasurementTechnique::None) == 0);
}

TEST_CASE("MeasurementTechnique: sci-fi techniques are present")
{
    // Verify future techniques exist (compile-time check + value ordering)
    CHECK(static_cast<u16>(MeasurementTechnique::NeutrinoDetection)   >
          static_cast<u16>(MeasurementTechnique::XRayObservation));
    CHECK(static_cast<u16>(MeasurementTechnique::GravitationalWave)    >
          static_cast<u16>(MeasurementTechnique::NeutrinoDetection));
    CHECK(static_cast<u16>(MeasurementTechnique::DirectNeuralImaging)  >
          static_cast<u16>(MeasurementTechnique::GravitationalWave));
}

// =================================================================
// PropertyDescriptor
// =================================================================

TEST_CASE("PropertyDescriptor fields are writable and readable")
{
    PropertyDescriptor desc;
    desc.name                      = "mag_v";
    desc.unlocks_at                = KnowledgeLevel::Detected;
    desc.required_technique        = MeasurementTechnique::BroadbandPhotometry;
    desc.required_snr              = 10.0f;
    desc.required_observation_hours = 0.5f;

    CHECK(desc.name                       == "mag_v");
    CHECK(desc.unlocks_at                 == KnowledgeLevel::Detected);
    CHECK(desc.required_technique         == MeasurementTechnique::BroadbandPhotometry);
    CHECK(desc.required_snr               == doctest::Approx(10.0f));
    CHECK(desc.required_observation_hours == doctest::Approx(0.5f));
}

// =================================================================
// MeasurementRecord
// =================================================================

TEST_CASE("MeasurementRecord default construction")
{
    const MeasurementRecord rec;
    CHECK(rec.value          == doctest::Approx(0.0));
    CHECK(rec.uncertainty    == doctest::Approx(0.0f));
    CHECK(rec.snr            == doctest::Approx(0.0f));
    CHECK(rec.session_id     == 0u);
    CHECK(rec.observation_jd == doctest::Approx(0.0));
}

TEST_CASE("MeasurementRecord holds assigned values")
{
    MeasurementRecord rec;
    rec.value          = 5.72;
    rec.uncertainty    = 0.01f;
    rec.snr            = 42.0f;
    rec.session_id     = 7u;
    rec.observation_jd = 2451545.0;

    CHECK(rec.value          == doctest::Approx(5.72));
    CHECK(rec.uncertainty    == doctest::Approx(0.01f));
    CHECK(rec.snr            == doctest::Approx(42.0f));
    CHECK(rec.session_id     == 7u);
    CHECK(rec.observation_jd == doctest::Approx(2451545.0));
}

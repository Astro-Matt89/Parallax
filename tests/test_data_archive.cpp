/// @file test_data_archive.cpp
/// @brief Unit tests for Task 8.6: DataArchive.
///
/// Tests add / query / clear and JSON round-trip, mirroring the style of
/// test_knowledge_database.cpp.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "observation/data_archive.hpp"
#include "observation/data_record.hpp"

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cstdint>
#include <memory>

using namespace parallax::observation;

// =============================================================================
// Helpers
// =============================================================================

namespace
{

std::unique_ptr<DataRecord> make_record(std::uint64_t id,
                                        std::uint64_t target_id = 0,
                                        DataType      type      = DataType::Mock)
{
    auto r               = std::make_unique<DataRecord>();
    r->id                = id;
    r->session_id        = 10u;
    r->target_object_id  = target_id;
    r->type              = type;
    r->technique         = "mock";
    r->observation_jd    = 2451545.0;
    r->duration_hours    = 1.0;
    r->achieved_snr      = 15.0;
    r->measurements["mag_v"] = 8.5;
    r->uncertainties["mag_v"] = 0.05f;
    return r;
}

} // anonymous namespace

// =============================================================================
// Default construction
// =============================================================================

TEST_CASE("Empty archive has size 0")
{
    DataArchive archive;
    CHECK(archive.size()    == 0u);
    CHECK(archive.get_all().empty());
    CHECK(archive.get_by_id(1u)        == nullptr);
    CHECK(archive.get_by_target(42u).empty());
}

// =============================================================================
// add / get_by_id
// =============================================================================

TEST_CASE("add stores record retrievable by id")
{
    DataArchive archive;
    archive.add(make_record(1u, 100u));

    CHECK(archive.size() == 1u);

    const DataRecord* r = archive.get_by_id(1u);
    REQUIRE(r != nullptr);
    CHECK(r->id               == 1u);
    CHECK(r->target_object_id == 100u);
}

TEST_CASE("add null pointer is silently ignored")
{
    DataArchive archive;
    archive.add(nullptr);
    CHECK(archive.size() == 0u);
}

TEST_CASE("add with duplicate id overwrites previous record")
{
    DataArchive archive;
    auto r1        = make_record(1u, 100u);
    auto r2        = make_record(1u, 200u); // same id, different target
    archive.add(std::move(r1));
    archive.add(std::move(r2));

    CHECK(archive.size() == 1u);
    const DataRecord* r = archive.get_by_id(1u);
    REQUIRE(r != nullptr);
    CHECK(r->target_object_id == 200u);
}

TEST_CASE("get_by_id returns nullptr for absent id")
{
    DataArchive archive;
    CHECK(archive.get_by_id(999u) == nullptr);
}

// =============================================================================
// get_by_target
// =============================================================================

TEST_CASE("get_by_target returns matching records")
{
    DataArchive archive;
    archive.add(make_record(1u, 42u));
    archive.add(make_record(2u, 42u));
    archive.add(make_record(3u, 99u));

    const auto hits = archive.get_by_target(42u);
    CHECK(hits.size() == 2u);
    for (const auto* r : hits)
    {
        CHECK(r->target_object_id == 42u);
    }
}

TEST_CASE("get_by_target returns empty for unknown target")
{
    DataArchive archive;
    archive.add(make_record(1u, 5u));
    CHECK(archive.get_by_target(999u).empty());
}

// =============================================================================
// get_all
// =============================================================================

TEST_CASE("get_all returns every record")
{
    DataArchive archive;
    archive.add(make_record(1u));
    archive.add(make_record(2u));
    archive.add(make_record(3u));

    const auto all = archive.get_all();
    CHECK(all.size() == 3u);
}

// =============================================================================
// clear
// =============================================================================

TEST_CASE("clear empties the archive")
{
    DataArchive archive;
    archive.add(make_record(1u));
    archive.add(make_record(2u));
    archive.clear();
    CHECK(archive.size() == 0u);
    CHECK(archive.get_all().empty());
}

TEST_CASE("remove_record removes existing record and reports success")
{
    DataArchive archive;
    archive.add(make_record(1u));
    archive.add(make_record(2u));

    CHECK(archive.remove_record(1u) == true);
    CHECK(archive.get_by_id(1u) == nullptr);
    CHECK(archive.size() == 1u);
}

TEST_CASE("remove_record returns false when id is missing")
{
    DataArchive archive;
    archive.add(make_record(5u));

    CHECK(archive.remove_record(999u) == false);
    CHECK(archive.size() == 1u);
}

// =============================================================================
// JSON round-trip
// =============================================================================

TEST_CASE("save / load round-trip produces identical state")
{
    DataArchive archive;

    // Photometric record
    {
        auto r               = make_record(10u, 1001u, DataType::PhotometricMeasurement);
        r->session_id        = 5u;
        r->technique         = "photometry";
        r->observation_jd    = 2460000.5;
        r->duration_hours    = 2.0;
        r->achieved_snr      = 50.0;
        r->measurements["mag_v"]   = 12.3;
        r->measurements["mag_b"]   = 13.1;
        r->uncertainties["mag_v"]  = 0.02f;
        r->uncertainties["mag_b"]  = 0.04f;
        archive.add(std::move(r));
    }

    // Mock record with raw_data
    {
        auto r               = make_record(20u, 2002u, DataType::Mock);
        r->raw_data          = {0x01, 0x02, 0x03};
        archive.add(std::move(r));
    }

    const auto tmp = std::filesystem::temp_directory_path() / "plx_test_data_archive.json";
    REQUIRE(archive.save(tmp));

    DataArchive archive2;
    REQUIRE(archive2.load(tmp));

    CHECK(archive2.size() == 2u);

    // Verify photometric record
    {
        const DataRecord* r = archive2.get_by_id(10u);
        REQUIRE(r != nullptr);
        CHECK(r->id               == 10u);
        CHECK(r->session_id       == 5u);
        CHECK(r->target_object_id == 1001u);
        CHECK(r->type             == DataType::PhotometricMeasurement);
        CHECK(r->technique        == "photometry");
        CHECK(r->observation_jd   == doctest::Approx(2460000.5));
        CHECK(r->duration_hours   == doctest::Approx(2.0));
        CHECK(r->achieved_snr     == doctest::Approx(50.0));
        REQUIRE(r->measurements.count("mag_v"));
        CHECK(r->measurements.at("mag_v") == doctest::Approx(12.3));
        REQUIRE(r->uncertainties.count("mag_v"));
        CHECK(r->uncertainties.at("mag_v") == doctest::Approx(0.02f));
    }

    // Verify mock record with raw_data
    {
        const DataRecord* r = archive2.get_by_id(20u);
        REQUIRE(r != nullptr);
        CHECK(r->type == DataType::Mock);
        REQUIRE(r->raw_data.size() == 3u);
        CHECK(r->raw_data[0] == 0x01);
        CHECK(r->raw_data[2] == 0x03);
    }

    std::filesystem::remove(tmp);
}

TEST_CASE("load returns false for non-existent file")
{
    DataArchive archive;
    CHECK(archive.load("/tmp/plx_archive_no_such_file_xyz.json") == false);
}

TEST_CASE("load returns false for malformed JSON")
{
    const auto tmp = std::filesystem::temp_directory_path() / "plx_archive_bad.json";
    {
        std::ofstream ofs(tmp);
        ofs << "{ not valid json {{{{ ";
    }
    DataArchive archive;
    CHECK(archive.load(tmp) == false);
    std::filesystem::remove(tmp);
}

TEST_CASE("load clears archive before loading")
{
    DataArchive archive;
    archive.add(make_record(99u));

    // Save empty archive
    DataArchive empty;
    const auto tmp = std::filesystem::temp_directory_path() / "plx_archive_empty.json";
    REQUIRE(empty.save(tmp));

    REQUIRE(archive.load(tmp));
    CHECK(archive.size() == 0u);

    std::filesystem::remove(tmp);
}

// =============================================================================
// All DataType enum values survive round-trip
// =============================================================================

TEST_CASE("all DataType values survive JSON round-trip")
{
    const std::pair<DataType, const char*> types[] = {
        { DataType::PhotometricMeasurement, "PhotometricMeasurement" },
        { DataType::LightCurve,             "LightCurve"             },
        { DataType::Spectrum,               "Spectrum"               },
        { DataType::Image,                  "Image"                  },
        { DataType::SurveySourceList,       "SurveySourceList"       },
        { DataType::Mock,                   "Mock"                   },
    };

    DataArchive archive;
    std::uint64_t id = 1u;
    for (const auto& [dt, _] : types)
    {
        archive.add(make_record(id++, 0u, dt));
    }

    const auto tmp = std::filesystem::temp_directory_path() / "plx_archive_types.json";
    REQUIRE(archive.save(tmp));

    DataArchive archive2;
    REQUIRE(archive2.load(tmp));
    CHECK(archive2.size() == 6u);

    id = 1u;
    for (const auto& [dt, _] : types)
    {
        const DataRecord* r = archive2.get_by_id(id++);
        REQUIRE(r != nullptr);
        CHECK(r->type == dt);
    }

    std::filesystem::remove(tmp);
}

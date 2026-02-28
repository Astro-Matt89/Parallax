/// @file test_catalog_loader.cpp
/// @brief Unit tests for parallax::catalog::CatalogLoader.
///
/// Verifies CSV parsing, degree-to-radian conversion, error handling,
/// and bright star catalog correctness against known reference values.

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "catalog/catalog_loader.hpp"
#include "catalog/star_entry.hpp"
#include "core/logger.hpp"
#include "core/types.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

using namespace parallax;
using namespace parallax::catalog;

// =================================================================
// Custom main: initialize logger before tests
// =================================================================

int main(int argc, char** argv)
{
    parallax::core::Logger::init();
    const int result = doctest::Context(argc, argv).run();
    parallax::core::Logger::shutdown();
    return result;
}

// =================================================================
// Helper: create a temporary CSV file for testing
// =================================================================

class TempCsvFile
{
public:
    explicit TempCsvFile(const std::string& filename, const std::string& content)
        : m_path(std::filesystem::temp_directory_path() / filename)
    {
        std::ofstream file(m_path);
        file << content;
    }

    ~TempCsvFile()
    {
        std::filesystem::remove(m_path);
    }

    [[nodiscard]] const std::filesystem::path& path() const { return m_path; }

    TempCsvFile(const TempCsvFile&) = delete;
    TempCsvFile& operator=(const TempCsvFile&) = delete;

private:
    std::filesystem::path m_path;
};

// =================================================================
// Tolerance
// =================================================================

static constexpr f64 kRadTol = 1e-4;  // ~0.006° — sufficient for degree->radian conversion
static constexpr f32 kMagTol = 0.01f;

// =================================================================
// Bright-star CSV loading
// =================================================================

TEST_CASE("Load bright_stars.csv from test data")
{
    const TempCsvFile csv("test_bright.csv",
        "Name,RA_deg,Dec_deg,Vmag,BV\n"
        "Sirius,101.287,-16.716,-1.46,0.009\n"
        "Vega,279.235,38.784,0.03,0.000\n"
        "Polaris,37.954,89.264,1.98,0.636\n"
    );

    const auto result = CatalogLoader::load_bright_star_csv(csv.path());

    REQUIRE(result.has_value());
    REQUIRE(result->size() == 3);

    SUBCASE("Sirius: RA, Dec, magnitude, color")
    {
        const auto& sirius = (*result)[0];
        CHECK(sirius.ra  == doctest::Approx(101.287 * astro_constants::kDegToRad).epsilon(kRadTol));
        CHECK(sirius.dec == doctest::Approx(-16.716 * astro_constants::kDegToRad).epsilon(kRadTol));
        CHECK(sirius.mag_v    == doctest::Approx(-1.46f).epsilon(kMagTol));
        CHECK(sirius.color_bv == doctest::Approx(0.009f).epsilon(kMagTol));
    }

    SUBCASE("Vega: RA, Dec, magnitude, color")
    {
        const auto& vega = (*result)[1];
        CHECK(vega.ra  == doctest::Approx(279.235 * astro_constants::kDegToRad).epsilon(kRadTol));
        CHECK(vega.dec == doctest::Approx(38.784 * astro_constants::kDegToRad).epsilon(kRadTol));
        CHECK(vega.mag_v    == doctest::Approx(0.03f).epsilon(kMagTol));
        CHECK(vega.color_bv == doctest::Approx(0.0f).epsilon(kMagTol));
    }

    SUBCASE("Polaris: Dec near +90 degrees")
    {
        const auto& polaris = (*result)[2];
        CHECK(polaris.dec == doctest::Approx(89.264 * astro_constants::kDegToRad).epsilon(kRadTol));
        CHECK(polaris.mag_v == doctest::Approx(1.98f).epsilon(kMagTol));
    }
}

TEST_CASE("Star count matches expected for full bright-star catalog")
{
    const TempCsvFile csv("test_full_bright.csv",
        "Name,RA_deg,Dec_deg,Vmag,BV\n"
        "Sirius,101.287,-16.716,-1.46,0.009\n"
        "Canopus,95.988,-52.696,-0.74,0.164\n"
        "Arcturus,213.915,19.182,-0.05,1.239\n"
        "Vega,279.235,38.784,0.03,0.000\n"
        "Capella,79.172,45.998,0.08,0.795\n"
        "Rigel,78.634,-8.202,0.13,-0.03\n"
        "Procyon,114.826,5.225,0.34,0.432\n"
        "Betelgeuse,88.793,7.407,0.42,1.850\n"
        "Achernar,24.429,-57.237,0.46,-0.158\n"
        "Hadar,210.956,-60.373,0.61,-0.231\n"
        "Altair,297.696,8.868,0.76,0.221\n"
        "Acrux,186.650,-63.099,0.76,-0.264\n"
        "Aldebaran,68.980,16.510,0.86,1.538\n"
        "Antares,247.352,-26.432,0.96,1.830\n"
        "Spica,201.298,-11.161,0.97,-0.235\n"
        "Pollux,116.329,28.026,1.14,0.991\n"
        "Fomalhaut,344.413,-29.622,1.16,0.145\n"
        "Deneb,310.358,45.280,1.25,0.092\n"
        "Mimosa,191.930,-59.689,1.25,-0.238\n"
        "Regulus,152.093,11.967,1.35,-0.114\n"
        "Polaris,37.954,89.264,1.98,0.636\n"
    );

    const auto result = CatalogLoader::load_bright_star_csv(csv.path());

    REQUIRE(result.has_value());
    CHECK(result->size() == 21);
}

// =================================================================
// Hipparcos CSV loading
// =================================================================

TEST_CASE("Load Hipparcos-format CSV")
{
    const TempCsvFile csv("test_hip.csv",
        "HIP,RA_deg,Dec_deg,Vmag,BV\n"
        "32349,101.287,-16.716,-1.46,0.009\n"
        "91262,279.235,38.784,0.03,0.000\n"
    );

    const auto result = CatalogLoader::load_hipparcos_csv(csv.path());

    REQUIRE(result.has_value());
    REQUIRE(result->size() == 2);

    // Check HIP IDs are correctly assigned
    CHECK((*result)[0].catalog_id == 32349);
    CHECK((*result)[1].catalog_id == 91262);

    // Check coordinates converted to radians
    CHECK((*result)[0].ra == doctest::Approx(101.287 * astro_constants::kDegToRad).epsilon(kRadTol));
}

// =================================================================
// Degree to radian conversion correctness
// =================================================================

TEST_CASE("RA 0 deg converts to 0 radians")
{
    const TempCsvFile csv("test_zero_ra.csv",
        "Name,RA_deg,Dec_deg,Vmag,BV\n"
        "TestStar,0.0,0.0,5.0,0.5\n"
    );

    const auto result = CatalogLoader::load_bright_star_csv(csv.path());

    REQUIRE(result.has_value());
    CHECK((*result)[0].ra  == doctest::Approx(0.0).epsilon(1e-10));
    CHECK((*result)[0].dec == doctest::Approx(0.0).epsilon(1e-10));
}

TEST_CASE("RA 180 deg converts to pi radians")
{
    const TempCsvFile csv("test_180_ra.csv",
        "Name,RA_deg,Dec_deg,Vmag,BV\n"
        "TestStar,180.0,0.0,5.0,0.5\n"
    );

    const auto result = CatalogLoader::load_bright_star_csv(csv.path());

    REQUIRE(result.has_value());
    CHECK((*result)[0].ra == doctest::Approx(astro_constants::kPi).epsilon(1e-10));
}

TEST_CASE("Negative Dec converts correctly")
{
    const TempCsvFile csv("test_neg_dec.csv",
        "Name,RA_deg,Dec_deg,Vmag,BV\n"
        "TestStar,0.0,-90.0,5.0,0.5\n"
    );

    const auto result = CatalogLoader::load_bright_star_csv(csv.path());

    REQUIRE(result.has_value());
    CHECK((*result)[0].dec == doctest::Approx(-astro_constants::kHalfPi).epsilon(1e-10));
}

// =================================================================
// Negative magnitude (bright stars)
// =================================================================

TEST_CASE("Negative magnitude parsed correctly")
{
    const TempCsvFile csv("test_neg_mag.csv",
        "Name,RA_deg,Dec_deg,Vmag,BV\n"
        "Sirius,101.287,-16.716,-1.46,0.009\n"
    );

    const auto result = CatalogLoader::load_bright_star_csv(csv.path());

    REQUIRE(result.has_value());
    CHECK((*result)[0].mag_v == doctest::Approx(-1.46f).epsilon(kMagTol));
}

TEST_CASE("Negative B-V color parsed correctly")
{
    const TempCsvFile csv("test_neg_bv.csv",
        "Name,RA_deg,Dec_deg,Vmag,BV\n"
        "Rigel,78.634,-8.202,0.13,-0.03\n"
    );

    const auto result = CatalogLoader::load_bright_star_csv(csv.path());

    REQUIRE(result.has_value());
    CHECK((*result)[0].color_bv == doctest::Approx(-0.03f).epsilon(kMagTol));
}

// =================================================================
// Error handling
// =================================================================

TEST_CASE("Non-existent file returns nullopt")
{
    const auto result = CatalogLoader::load_bright_star_csv("this_file_does_not_exist.csv");
    CHECK_FALSE(result.has_value());
}

TEST_CASE("Empty file (header only) returns nullopt")
{
    const TempCsvFile csv("test_empty.csv",
        "Name,RA_deg,Dec_deg,Vmag,BV\n"
    );

    const auto result = CatalogLoader::load_bright_star_csv(csv.path());
    CHECK_FALSE(result.has_value());
}

TEST_CASE("Malformed lines are skipped, valid lines still loaded")
{
    const TempCsvFile csv("test_malformed.csv",
        "Name,RA_deg,Dec_deg,Vmag,BV\n"
        "Sirius,101.287,-16.716,-1.46,0.009\n"
        "Bad,not_a_number,bad,bad,bad\n"
        "Vega,279.235,38.784,0.03,0.000\n"
        "Incomplete,101.287\n"
    );

    const auto result = CatalogLoader::load_bright_star_csv(csv.path());

    REQUIRE(result.has_value());
    CHECK(result->size() == 2);  // Sirius and Vega; malformed lines skipped
}

// =================================================================
// Stars sorted by magnitude (verify ordering from test catalog)
// =================================================================

TEST_CASE("Brightest star in catalog is Sirius (mag -1.46)")
{
    const TempCsvFile csv("test_brightness.csv",
        "Name,RA_deg,Dec_deg,Vmag,BV\n"
        "Vega,279.235,38.784,0.03,0.000\n"
        "Sirius,101.287,-16.716,-1.46,0.009\n"
        "Polaris,37.954,89.264,1.98,0.636\n"
    );

    const auto result = CatalogLoader::load_bright_star_csv(csv.path());
    REQUIRE(result.has_value());

    // Find the brightest star (lowest magnitude)
    f32 min_mag = 100.0f;
    for (const auto& star : *result)
    {
        if (star.mag_v < min_mag)
        {
            min_mag = star.mag_v;
        }
    }

    CHECK(min_mag == doctest::Approx(-1.46f).epsilon(kMagTol));
}
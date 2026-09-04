/// @file test_image_exporter.cpp
/// @brief Unit tests for ImageExporter (Sprint 10a Task 10a.5).

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "imaging/image_exporter.hpp"

#include "core/logger.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

// =================================================================
// Custom main: initialize logger before tests
//
// ImageExporter::export_fits() logs through PLX_CORE_WARN, which
// dereferences Logger::get_core_logger().  Without Logger::init() that
// shared_ptr is null and the call segfaults.  Same pattern as
// test_catalog_loader.cpp.
// =================================================================

int main(int argc, char** argv)
{
    parallax::core::Logger::init();
    const int result = doctest::Context(argc, argv).run();
    parallax::core::Logger::shutdown();
    return result;
}

namespace parallax::imaging
{
    namespace
    {
        class ScopedTempDir
        {
        public:
            ScopedTempDir()
                : m_path(std::filesystem::temp_directory_path() / unique_name())
            {
                std::filesystem::create_directories(m_path);
            }

            ~ScopedTempDir()
            {
                std::error_code ec;
                std::filesystem::remove_all(m_path, ec);
            }

            [[nodiscard]] const std::filesystem::path& path() const
            {
                return m_path;
            }

        private:
            [[nodiscard]] static std::string unique_name()
            {
                const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
                std::mt19937_64 rng(static_cast<u64>(now));
                return "parallax_test_image_exporter_" + std::to_string(rng());
            }

            std::filesystem::path m_path;
        };

        [[nodiscard]] MultispectralImage make_test_multispectral_image()
        {
            MultispectralImage image(4, 3, 1.5);

            image.emplace_band(ImageMetadata{
                .band_name = "Visible",
                .band_index = 0,
                .wavelength_nm = 550.0,
                .pixel_scale_arcsec_per_px = 1.5,
            });

            image.emplace_band(ImageMetadata{
                .band_name = "Near-IR",
                .band_index = 1,
                .wavelength_nm = 1600.0,
                .pixel_scale_arcsec_per_px = 1.5,
            });

            // Take the band references only after every band exists: bands live in
            // a std::vector, so the second emplace_band can reallocate and would
            // leave a reference returned by the first one dangling.
            Image& visible = image.band(0);
            Image& nir     = image.band(1);

            for (u32 y = 0; y < image.height(); ++y)
            {
                for (u32 x = 0; x < image.width(); ++x)
                {
                    const f32 base = static_cast<f32>(y * image.width() + x + 1);
                    visible(x, y) = base;
                    nir(x, y) = base * 2.0f;
                }
            }

            return image;
        }

    }

    TEST_CASE("export_fits is currently a stub and returns an empty path")
    {
        const ScopedTempDir temp_dir;
        const MultispectralImage image = make_test_multispectral_image();

        ExportMetadata metadata;
        metadata.object_name = "HIP 32005";
        metadata.target_ra_rad = 1.234;
        metadata.target_dec_rad = -0.321;
        metadata.date_obs_utc = "2026-07-11T12:00:00Z";
        metadata.exposure_time_s = 120.0;
        metadata.instrument_name = "Glasswing Array";
        metadata.band_snr = {25.5, 12.25};

        const std::filesystem::path fits_path = ImageExporter::export_fits(
            image,
            "image_export_test.fits",
            metadata,
            temp_dir.path());

        CHECK(fits_path.empty());
    }

    TEST_CASE("stretch_to_u16 linear maps min and max to full range")
    {
        const std::array<f32, 3> pixels = {10.0f, 20.0f, 30.0f};
        const std::vector<u16> stretched = ImageExporter::stretch_to_u16(
            pixels,
            StretchMode::Linear,
            {});

        REQUIRE(stretched.size() == pixels.size());
        CHECK(stretched.front() == 0u);
        CHECK(stretched.back() == 65535u);
    }

    TEST_CASE("stretch_to_u16 log and asinh are monotonic and map max to full scale")
    {
        const std::array<f32, 5> pixels = {0.0f, 1.0f, 2.0f, 4.0f, 8.0f};

        const std::vector<u16> stretched_log = ImageExporter::stretch_to_u16(
            pixels,
            StretchMode::Log,
            StretchParameters{.log_k = 250.0, .asinh_softening = 10.0});

        const std::vector<u16> stretched_asinh = ImageExporter::stretch_to_u16(
            pixels,
            StretchMode::Asinh,
            StretchParameters{.log_k = 1000.0, .asinh_softening = 0.5});

        REQUIRE(stretched_log.size() == pixels.size());
        REQUIRE(stretched_asinh.size() == pixels.size());

        for (std::size_t i = 1; i < pixels.size(); ++i)
        {
            CHECK(stretched_log[i] >= stretched_log[i - 1]);
            CHECK(stretched_asinh[i] >= stretched_asinh[i - 1]);
        }

        CHECK(stretched_log.back() == 65535u);
        CHECK(stretched_asinh.back() == 65535u);
    }

    TEST_CASE("export_png_single_band writes a readable PNG with expected dimensions")
    {
        const ScopedTempDir temp_dir;
        MultispectralImage image = make_test_multispectral_image();

        const std::filesystem::path png_path = ImageExporter::export_png_single_band(
            image.band(0),
            "single_band.png",
            StretchMode::Asinh,
            StretchParameters{.log_k = 1000.0, .asinh_softening = 0.75},
            temp_dir.path());

        REQUIRE(std::filesystem::exists(png_path));
        REQUIRE(std::filesystem::file_size(png_path) > 0);

        int width = 0;
        int height = 0;
        int channels = 0;
        const int info_ok = stbi_info(png_path.string().c_str(), &width, &height, &channels);

        REQUIRE(info_ok != 0);
        CHECK(width == static_cast<int>(image.width()));
        CHECK(height == static_cast<int>(image.height()));
        CHECK(channels == 1);
    }

    TEST_CASE("export_png_false_color writes RGB PNG with expected dimensions")
    {
        const ScopedTempDir temp_dir;
        const MultispectralImage image = make_test_multispectral_image();

        const std::filesystem::path png_path = ImageExporter::export_png_false_color(
            image,
            "false_color.png",
            1,
            0,
            0,
            StretchMode::Linear,
            {},
            temp_dir.path());

        REQUIRE(std::filesystem::exists(png_path));
        REQUIRE(std::filesystem::file_size(png_path) > 0);

        int width = 0;
        int height = 0;
        int channels = 0;
        const int info_ok = stbi_info(png_path.string().c_str(), &width, &height, &channels);

        REQUIRE(info_ok != 0);
        CHECK(width == static_cast<int>(image.width()));
        CHECK(height == static_cast<int>(image.height()));
        CHECK(channels == 3);
    }

} // namespace parallax::imaging

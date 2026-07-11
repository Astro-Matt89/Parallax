/// @file test_image_exporter.cpp
/// @brief Unit tests for ImageExporter (Sprint 10a Task 10a.5).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "imaging/image_exporter.hpp"

#include <fitsio.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

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

            Image& visible = image.emplace_band(ImageMetadata{
                .band_name = "Visible",
                .band_index = 0,
                .wavelength_nm = 550.0,
                .pixel_scale_arcsec_per_px = 1.5,
            });

            Image& nir = image.emplace_band(ImageMetadata{
                .band_name = "Near-IR",
                .band_index = 1,
                .wavelength_nm = 1600.0,
                .pixel_scale_arcsec_per_px = 1.5,
            });

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

        [[nodiscard]] f64 read_key_double(fitsfile* fits, const char* key)
        {
            int status = 0;
            f64 value = 0.0;
            fits_read_key(fits, TDOUBLE, key, &value, nullptr, &status);
            REQUIRE(status == 0);
            return value;
        }

        [[nodiscard]] std::string read_key_string(fitsfile* fits, const char* key)
        {
            int status = 0;
            char value[FLEN_VALUE] = {};
            fits_read_key(fits, TSTRING, key, value, nullptr, &status);
            REQUIRE(status == 0);
            return std::string(value);
        }
    }

    TEST_CASE("export_fits writes multi-extension float FITS with metadata and WCS")
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

        REQUIRE(std::filesystem::exists(fits_path));
        REQUIRE(std::filesystem::file_size(fits_path) > 0);

        fitsfile* fits = nullptr;
        int status = 0;
        fits_open_file(&fits, fits_path.string().c_str(), READONLY, &status);
        REQUIRE(status == 0);

        int hdu_count = 0;
        fits_get_num_hdus(fits, &hdu_count, &status);
        REQUIRE(status == 0);
        CHECK(hdu_count == 3); // primary + 2 bands

        int hdu_type = 0;
        fits_movabs_hdu(fits, 1, &hdu_type, &status);
        REQUIRE(status == 0);
        CHECK(read_key_string(fits, "OBJECT") == "HIP 32005");
        CHECK(read_key_double(fits, "EXPTIME") == doctest::Approx(120.0));
        CHECK(read_key_string(fits, "BAND001") == "Visible");
        CHECK(read_key_string(fits, "BAND002") == "Near-IR");
        CHECK(read_key_double(fits, "WAVE001") == doctest::Approx(550.0));
        CHECK(read_key_double(fits, "SNR001") == doctest::Approx(25.5));

        for (int hdu_index = 2; hdu_index <= 3; ++hdu_index)
        {
            fits_movabs_hdu(fits, hdu_index, &hdu_type, &status);
            REQUIRE(status == 0);

            int bitpix = 0;
            int naxis = 0;
            long axes[2] = {0, 0};
            fits_get_img_param(fits, 2, &bitpix, &naxis, axes, &status);
            REQUIRE(status == 0);
            CHECK(bitpix == FLOAT_IMG);
            CHECK(naxis == 2);
            CHECK(axes[0] == 4);
            CHECK(axes[1] == 3);

            const std::string band_name = read_key_string(fits, "BANDNAME");
            if (hdu_index == 2)
            {
                CHECK(band_name == "Visible");
                CHECK(read_key_double(fits, "WAVELEN") == doctest::Approx(550.0));
                CHECK(read_key_double(fits, "SNR") == doctest::Approx(25.5));
            }
            else
            {
                CHECK(band_name == "Near-IR");
                CHECK(read_key_double(fits, "WAVELEN") == doctest::Approx(1600.0));
                CHECK(read_key_double(fits, "SNR") == doctest::Approx(12.25));
            }

            const f64 expected_cdelt = 1.5 / 3600.0;
            CHECK(read_key_double(fits, "CRVAL1") == doctest::Approx(metadata.target_ra_rad * astro_constants::kRadToDeg));
            CHECK(read_key_double(fits, "CRVAL2") == doctest::Approx(metadata.target_dec_rad * astro_constants::kRadToDeg));
            CHECK(read_key_double(fits, "CDELT1") == doctest::Approx(-expected_cdelt));
            CHECK(read_key_double(fits, "CDELT2") == doctest::Approx(expected_cdelt));
            CHECK(read_key_string(fits, "CTYPE1") == "RA---TAN");
            CHECK(read_key_string(fits, "CTYPE2") == "DEC--TAN");
            CHECK(read_key_string(fits, "CUNIT1") == "deg");
            CHECK(read_key_string(fits, "CUNIT2") == "deg");
        }

        fits_close_file(fits, &status);
        REQUIRE(status == 0);
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

/// @file image_exporter.cpp
/// @brief FITS/PNG export implementation for Sprint 10a Task 10a.5.

#include "imaging/image_exporter.hpp"

#include "core/types.hpp"
#include "core/user_data_path.hpp"

#include <fitsio.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace parallax::imaging
{
    namespace
    {
        using astro_constants::kRadToDeg;

        [[nodiscard]] std::string fits_error_message(int status)
        {
            char buffer[FLEN_STATUS] = {};
            fits_get_errstatus(status, buffer);
            return std::string(buffer);
        }

        void check_fits_status(int status, const char* operation)
        {
            if (status != 0)
            {
                throw std::runtime_error(std::string(operation) + ": " + fits_error_message(status));
            }
        }

        [[nodiscard]] std::filesystem::path resolve_output_path(
            const std::filesystem::path& filename,
            const std::filesystem::path& output_directory)
        {
            const std::filesystem::path default_dir = output_directory.empty()
                ? (core::user_data_save_dir() / "exports")
                : output_directory;
            const std::filesystem::path output_path = filename.is_absolute()
                ? filename
                : default_dir / filename;
            std::filesystem::create_directories(output_path.parent_path());

            if (filename.is_absolute())
            {
                return filename;
            }
            return output_path;
        }

        void write_primary_metadata(
            fitsfile*                        fits,
            const ExportMetadata&            metadata,
            std::size_t                      band_count,
            int*                             status)
        {
            std::string object = metadata.object_name;
            std::string date   = metadata.date_obs_utc;
            std::string instrument = metadata.instrument_name;

            f64 ra_deg = metadata.target_ra_rad * kRadToDeg;
            f64 dec_deg = metadata.target_dec_rad * kRadToDeg;
            f64 exptime = metadata.exposure_time_s;
            i32 bands_i32 = static_cast<i32>(band_count);

            fits_update_key(fits, TSTRING, "OBJECT", object.data(), nullptr, status);
            fits_update_key(fits, TDOUBLE, "RA", &ra_deg, nullptr, status);
            fits_update_key(fits, TDOUBLE, "DEC", &dec_deg, nullptr, status);
            fits_update_key(fits, TSTRING, "DATE-OBS", date.data(), nullptr, status);
            fits_update_key(fits, TDOUBLE, "EXPTIME", &exptime, nullptr, status);
            fits_update_key(fits, TSTRING, "INSTRUME", instrument.data(), nullptr, status);
            fits_update_key(fits, TINT, "BANDCNT", &bands_i32, nullptr, status);
        }

        void write_primary_band_summary(
            fitsfile*                        fits,
            const MultispectralImage&        image,
            const ExportMetadata&            metadata,
            int*                             status)
        {
            for (std::size_t i = 0; i < image.band_count(); ++i)
            {
                const Image& band = image.band(i);
                std::string band_name = band.metadata().band_name;
                f64 wavelength = band.metadata().wavelength_nm;
                f64 snr = metadata.band_snr.at(i);

                char band_key[9] = {};
                char wave_key[9] = {};
                char snr_key[9] = {};
                (void)std::snprintf(band_key, sizeof(band_key), "BAND%03u", static_cast<u32>(i + 1));
                (void)std::snprintf(wave_key, sizeof(wave_key), "WAVE%03u", static_cast<u32>(i + 1));
                (void)std::snprintf(snr_key, sizeof(snr_key), "SNR%03u", static_cast<u32>(i + 1));

                fits_update_key(fits, TSTRING, band_key, band_name.data(), nullptr, status);
                fits_update_key(fits, TDOUBLE, wave_key, &wavelength, nullptr, status);
                fits_update_key(fits, TDOUBLE, snr_key, &snr, nullptr, status);
            }
        }

        void write_band_headers(
            fitsfile*                        fits,
            const Image&                     band,
            const ExportMetadata&            metadata,
            f64                              snr,
            f64                              cdelt_deg,
            int*                             status)
        {
            std::string band_name = band.metadata().band_name;
            std::string ctype1 = "RA---TAN";
            std::string ctype2 = "DEC--TAN";
            std::string cunit = "deg";

            f64 ra_deg = metadata.target_ra_rad * kRadToDeg;
            f64 dec_deg = metadata.target_dec_rad * kRadToDeg;
            f64 crpix1 = (static_cast<f64>(band.width()) + 1.0) * 0.5;
            f64 crpix2 = (static_cast<f64>(band.height()) + 1.0) * 0.5;
            f64 cdelt1 = -cdelt_deg;
            f64 cdelt2 = cdelt_deg;
            f64 wavelength = band.metadata().wavelength_nm;

            fits_update_key(fits, TSTRING, "BANDNAME", band_name.data(), nullptr, status);
            fits_update_key(fits, TDOUBLE, "WAVELEN", &wavelength, nullptr, status);
            fits_update_key(fits, TDOUBLE, "SNR", &snr, nullptr, status);

            fits_update_key(fits, TDOUBLE, "CRVAL1", &ra_deg, nullptr, status);
            fits_update_key(fits, TDOUBLE, "CRVAL2", &dec_deg, nullptr, status);
            fits_update_key(fits, TDOUBLE, "CRPIX1", &crpix1, nullptr, status);
            fits_update_key(fits, TDOUBLE, "CRPIX2", &crpix2, nullptr, status);
            fits_update_key(fits, TDOUBLE, "CDELT1", &cdelt1, nullptr, status);
            fits_update_key(fits, TDOUBLE, "CDELT2", &cdelt2, nullptr, status);
            fits_update_key(fits, TSTRING, "CTYPE1", ctype1.data(), nullptr, status);
            fits_update_key(fits, TSTRING, "CTYPE2", ctype2.data(), nullptr, status);
            fits_update_key(fits, TSTRING, "CUNIT1", cunit.data(), nullptr, status);
            fits_update_key(fits, TSTRING, "CUNIT2", cunit.data(), nullptr, status);
        }

        [[nodiscard]] std::vector<u8> quantize_u16_to_u8(std::span<const u16> pixels)
        {
            std::vector<u8> out(pixels.size(), 0);
            for (std::size_t i = 0; i < pixels.size(); ++i)
            {
                out[i] = static_cast<u8>(pixels[i] >> 8);
            }
            return out;
        }

        [[nodiscard]] f64 apply_stretch(
            f64 value,
            f64 min_value,
            f64 max_value,
            StretchMode stretch_mode,
            const StretchParameters& stretch_params)
        {
            const f64 shifted_value = std::max(value - min_value, 0.0);
            const f64 range = std::max(max_value - min_value, 0.0);
            if (range <= std::numeric_limits<f64>::epsilon())
            {
                return 0.0;
            }

            switch (stretch_mode)
            {
                case StretchMode::Linear:
                {
                    return shifted_value / range;
                }
                case StretchMode::Log:
                {
                    const f64 k = std::max(stretch_params.log_k, std::numeric_limits<f64>::epsilon());
                    const f64 denom = std::log1p(k * range);
                    if (denom <= std::numeric_limits<f64>::epsilon())
                    {
                        return 0.0;
                    }
                    return std::log1p(k * shifted_value) / denom;
                }
                case StretchMode::Asinh:
                {
                    const f64 beta = std::max(stretch_params.asinh_softening, std::numeric_limits<f64>::epsilon());
                    const f64 denom = std::asinh(range / beta);
                    if (denom <= std::numeric_limits<f64>::epsilon())
                    {
                        return 0.0;
                    }
                    return std::asinh(shifted_value / beta) / denom;
                }
            }

            return 0.0;
        }

        [[nodiscard]] std::size_t band_index_or_throw(
            const MultispectralImage& image,
            std::size_t index,
            const char* channel_name)
        {
            if (index >= image.band_count())
            {
                throw std::out_of_range(std::string("Invalid ") + channel_name + " band index");
            }
            return index;
        }
    }

    std::filesystem::path ImageExporter::export_fits(
        const MultispectralImage&      image,
        const std::filesystem::path&   filename,
        const ExportMetadata&          metadata,
        const std::filesystem::path&   output_directory)
    {
        if (metadata.band_snr.size() != image.band_count())
        {
            throw std::invalid_argument("ExportMetadata.band_snr must contain one value per image band");
        }

        const std::filesystem::path output_path = resolve_output_path(filename, output_directory);
        const std::string clobber_path = "!" + output_path.string();

        fitsfile* fits = nullptr;
        int status = 0;

        try
        {
            fits_create_file(&fits, clobber_path.c_str(), &status);
            check_fits_status(status, "fits_create_file");

            fits_create_img(fits, FLOAT_IMG, 0, nullptr, &status);
            check_fits_status(status, "fits_create_img(primary)");

            write_primary_metadata(fits, metadata, image.band_count(), &status);
            check_fits_status(status, "fits_update_key(primary headers)");
            write_primary_band_summary(fits, image, metadata, &status);
            check_fits_status(status, "fits_update_key(primary band summary)");

            for (std::size_t band_index = 0; band_index < image.band_count(); ++band_index)
            {
                const Image& band = image.band(band_index);
                long axes[2] = {
                    static_cast<long>(band.width()),
                    static_cast<long>(band.height())
                };
                fits_create_img(fits, FLOAT_IMG, 2, axes, &status);
                check_fits_status(status, "fits_create_img(extension)");

                const f64 cdelt_deg = band.metadata().pixel_scale_arcsec_per_px / 3600.0;
                const f64 snr = metadata.band_snr.at(band_index);
                write_band_headers(fits, band, metadata, snr, cdelt_deg, &status);
                check_fits_status(status, "fits_update_key(extension headers)");

                const LONGLONG first_pixel = 1;
                const LONGLONG pixel_count = static_cast<LONGLONG>(band.width()) * static_cast<LONGLONG>(band.height());
                fits_write_img(
                    fits,
                    TFLOAT,
                    first_pixel,
                    pixel_count,
                    const_cast<f32*>(band.data()),
                    &status);
                check_fits_status(status, "fits_write_img");
            }

            fits_close_file(fits, &status);
            check_fits_status(status, "fits_close_file");
            return output_path;
        }
        catch (...)
        {
            if (fits != nullptr)
            {
                (void)fits_close_file(fits, &status);
            }
            throw;
        }
    }

    std::filesystem::path ImageExporter::export_png_single_band(
        const Image&                   image,
        const std::filesystem::path&   filename,
        StretchMode                    stretch_mode,
        const StretchParameters&       stretch_params,
        const std::filesystem::path&   output_directory)
    {
        const std::filesystem::path output_path = resolve_output_path(filename, output_directory);

        const std::vector<u16> stretched_u16 = stretch_to_u16(image.pixels(), stretch_mode, stretch_params);
        const std::vector<u8> png_pixels = quantize_u16_to_u8(stretched_u16);

        const int write_ok = stbi_write_png(
            output_path.string().c_str(),
            static_cast<int>(image.width()),
            static_cast<int>(image.height()),
            1,
            png_pixels.data(),
            static_cast<int>(image.width()));

        if (write_ok == 0)
        {
            throw std::runtime_error("stbi_write_png failed for single-band export");
        }

        return output_path;
    }

    std::filesystem::path ImageExporter::export_png_false_color(
        const MultispectralImage&      image,
        const std::filesystem::path&   filename,
        std::size_t                    red_band_index,
        std::size_t                    green_band_index,
        std::size_t                    blue_band_index,
        StretchMode                    stretch_mode,
        const StretchParameters&       stretch_params,
        const std::filesystem::path&   output_directory)
    {
        const std::filesystem::path output_path = resolve_output_path(filename, output_directory);

        const std::size_t red_index = band_index_or_throw(image, red_band_index, "red");
        const std::size_t green_index = band_index_or_throw(image, green_band_index, "green");
        const std::size_t blue_index = band_index_or_throw(image, blue_band_index, "blue");

        const std::vector<u16> red_u16 = stretch_to_u16(image.band(red_index).pixels(), stretch_mode, stretch_params);
        const std::vector<u16> green_u16 = stretch_to_u16(image.band(green_index).pixels(), stretch_mode, stretch_params);
        const std::vector<u16> blue_u16 = stretch_to_u16(image.band(blue_index).pixels(), stretch_mode, stretch_params);

        const std::size_t pixel_count = red_u16.size();
        std::vector<u8> rgb8(pixel_count * 3, 0);
        for (std::size_t i = 0; i < pixel_count; ++i)
        {
            rgb8[3 * i + 0] = static_cast<u8>(red_u16[i] >> 8);
            rgb8[3 * i + 1] = static_cast<u8>(green_u16[i] >> 8);
            rgb8[3 * i + 2] = static_cast<u8>(blue_u16[i] >> 8);
        }

        const int stride_bytes = static_cast<int>(image.width() * 3);
        const int write_ok = stbi_write_png(
            output_path.string().c_str(),
            static_cast<int>(image.width()),
            static_cast<int>(image.height()),
            3,
            rgb8.data(),
            stride_bytes);

        if (write_ok == 0)
        {
            throw std::runtime_error("stbi_write_png failed for false-color export");
        }

        return output_path;
    }

    std::vector<u16> ImageExporter::stretch_to_u16(
        std::span<const f32>           pixels,
        StretchMode                    stretch_mode,
        const StretchParameters&       stretch_params)
    {
        std::vector<u16> output(pixels.size(), 0);
        if (pixels.empty())
        {
            return output;
        }

        const auto [min_it, max_it] = std::minmax_element(pixels.begin(), pixels.end());
        const f64 min_value = static_cast<f64>(*min_it);
        const f64 max_value = static_cast<f64>(*max_it);

        for (std::size_t i = 0; i < pixels.size(); ++i)
        {
            f64 normalized = apply_stretch(
                static_cast<f64>(pixels[i]),
                min_value,
                max_value,
                stretch_mode,
                stretch_params);
            normalized = std::clamp(normalized, 0.0, 1.0);
            output[i] = static_cast<u16>(std::lround(normalized * 65535.0));
        }

        return output;
    }

} // namespace parallax::imaging

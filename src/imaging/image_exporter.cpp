/// @file image_exporter.cpp
/// @brief FITS/PNG export implementation for Sprint 10a Task 10a.5.

#include "imaging/image_exporter.hpp"

#include "core/logger.hpp"
#include "core/types.hpp"
#include "core/user_data_path.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace parallax::imaging
{
    namespace
    {
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
        (void)image;
        (void)filename;
        (void)metadata;
        (void)output_directory;

        // TODO: implement minimal FITS writer (no external dependency)
        PLX_CORE_WARN("FITS export not yet implemented");
        return {};
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

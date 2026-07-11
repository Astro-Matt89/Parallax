#pragma once

/// @file image_exporter.hpp
/// @brief FITS and PNG image export for Sprint 10a total-power imaging.

#include "imaging/image.hpp"
#include "imaging/multispectral_image.hpp"

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace parallax::imaging
{
    /// @brief Stretch mapping applied before PNG export.
    enum class StretchMode
    {
        Linear,
        Log,
        Asinh,
    };

    /// @brief Per-export metadata written into FITS headers.
    struct ExportMetadata
    {
        std::string       object_name {"Unknown"};   ///< OBJECT
        f64               target_ra_rad {0.0};        ///< RA center (radians)
        f64               target_dec_rad {0.0};       ///< DEC center (radians)
        std::string       date_obs_utc {""};         ///< DATE-OBS (ISO-8601 UTC)
        f64               exposure_time_s {0.0};      ///< EXPTIME
        std::string       instrument_name {""};      ///< INSTRUME
        std::vector<f64>  band_snr;                   ///< One SNR value per band (required)
    };

    /// @brief Parameters for nonlinear PNG stretches.
    struct StretchParameters
    {
        f64 log_k = 1000.0;          ///< Log stretch constant k in log1p(k*x) normalization.
        f64 asinh_softening = 10.0;  ///< Asinh softening β in asinh(x/β).
    };

    /// @brief Export imaging products to FITS and PNG files.
    class ImageExporter
    {
    public:
        /// @brief Export all bands as a multi-extension FITS file.
        ///
        /// Layout:
        /// - Primary HDU: NAXIS=0 with observation metadata headers
        /// - One FLOAT_IMG extension per band (BITPIX=-32) with BANDNAME/WAVELEN/SNR and WCS
        ///
        /// The output path is resolved to `<user_data>/exports` by default and created if needed.
        /// Existing files are overwritten via cfitsio `!filename` clobber semantics.
        [[nodiscard]] static std::filesystem::path export_fits(
            const MultispectralImage&      image,
            const std::filesystem::path&   filename,
            const ExportMetadata&          metadata,
            const std::filesystem::path&   output_directory = {});

        /// @brief Export a single band PNG using the selected stretch.
        ///
        /// Pixel values are stretched to 16-bit [0..65535], then quantized to 8-bit for
        /// stb_image_write's PNG writer.
        [[nodiscard]] static std::filesystem::path export_png_single_band(
            const Image&                   image,
            const std::filesystem::path&   filename,
            StretchMode                    stretch_mode,
            const StretchParameters&       stretch_params = {},
            const std::filesystem::path&   output_directory = {});

        /// @brief Export a false-color PNG composite from three band indices.
        ///
        /// Typical mapping for Sprint 10a: NIR->R, Visible->G, Visible->B.
        [[nodiscard]] static std::filesystem::path export_png_false_color(
            const MultispectralImage&      image,
            const std::filesystem::path&   filename,
            std::size_t                    red_band_index,
            std::size_t                    green_band_index,
            std::size_t                    blue_band_index,
            StretchMode                    stretch_mode,
            const StretchParameters&       stretch_params = {},
            const std::filesystem::path&   output_directory = {});

        /// @brief Stretch scalar pixels to 16-bit normalized values.
        ///
        /// Linear: (v-min)/(max-min)
        /// Log:    log1p(k*(v-min)) / log1p(k*(max-min))
        /// Asinh:  asinh((v-min)/β) / asinh((max-min)/β)
        [[nodiscard]] static std::vector<u16> stretch_to_u16(
            std::span<const f32>           pixels,
            StretchMode                    stretch_mode,
            const StretchParameters&       stretch_params = {});
    };

} // namespace parallax::imaging

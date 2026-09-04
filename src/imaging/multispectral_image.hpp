#pragma once

/// @file multispectral_image.hpp
/// @brief Collection of single-band Image objects sharing the same geometry.
///
/// Each band occupies one Image slot in the internal vector.  All bands share
/// the same width, height, and pixel_scale; individual bands carry their own
/// wavelength and band_name in their ImageMetadata.

#include "imaging/image.hpp"

#include "core/types.hpp"

#include <span>
#include <stdexcept>
#include <vector>

namespace parallax::imaging
{
    /// @brief A set of co-registered Image planes, one per active spectral band.
    ///
    /// Geometry (width, height, pixel_scale_arcsec_per_px) is shared across all
    /// bands and is fixed at construction.  Use @c add_band to append per-band
    /// images; use @c band() / @c bands() to access them.
    class MultispectralImage
    {
    public:
        /// @brief Construct with shared geometry.
        /// @param width                    Detector width in pixels.
        /// @param height                   Detector height in pixels.
        /// @param pixel_scale_arcsec_per_px Angular pixel scale (arcsec/px).
        MultispectralImage(u32 width, u32 height, f64 pixel_scale_arcsec_per_px = 1.0)
            : m_width(width)
            , m_height(height)
            , m_pixel_scale(pixel_scale_arcsec_per_px)
        {
        }

        // Shared geometry accessors -----------------------------------------

        /// @brief Detector width shared by all bands (pixels).
        [[nodiscard]] u32 width() const { return m_width; }

        /// @brief Detector height shared by all bands (pixels).
        [[nodiscard]] u32 height() const { return m_height; }

        /// @brief Angular pixel scale shared by all bands (arcsec/px).
        [[nodiscard]] f64 pixel_scale_arcsec_per_px() const { return m_pixel_scale; }

        // Band management ---------------------------------------------------

        /// @brief Number of band planes currently stored.
        [[nodiscard]] std::size_t band_count() const { return m_bands.size(); }

        /// @brief Append a new band Image (must match the shared geometry).
        ///
        /// The band's own pixel_scale_arcsec_per_px metadata is overwritten to
        /// ensure consistency with the shared geometry.
        ///
        /// @throws std::invalid_argument if the image geometry does not match.
        void add_band(Image image)
        {
            if (image.width() != m_width || image.height() != m_height)
            {
                throw std::invalid_argument(
                    "MultispectralImage::add_band — image geometry does not match shared geometry");
            }
            image.metadata().pixel_scale_arcsec_per_px = m_pixel_scale;
            m_bands.push_back(std::move(image));
        }

        /// @brief Construct and append a band image in-place.
        ///
        /// The image is zero-initialised; metadata (other than pixel_scale) must
        /// be set on the returned reference if needed, or passed in via @p meta.
        ///
        /// @warning Bands are stored in a std::vector, so a later add_band() or
        /// emplace_band() can reallocate and invalidate the returned reference.
        /// Use it before adding the next band, or re-acquire it via band(index).
        Image& emplace_band(ImageMetadata meta = {})
        {
            meta.pixel_scale_arcsec_per_px = m_pixel_scale;
            meta.band_index = static_cast<u32>(m_bands.size());
            m_bands.emplace_back(m_width, m_height, std::move(meta));
            return m_bands.back();
        }

        // Band access -------------------------------------------------------

        /// @brief Indexed access to a band image (bounds-checked).
        [[nodiscard]] const Image& band(std::size_t index) const
        {
            if (index >= m_bands.size())
            {
                throw std::out_of_range("MultispectralImage::band — index out of range");
            }
            return m_bands[index];
        }

        [[nodiscard]] Image& band(std::size_t index)
        {
            if (index >= m_bands.size())
            {
                throw std::out_of_range("MultispectralImage::band — index out of range");
            }
            return m_bands[index];
        }

        /// @brief Non-owning span over all band images (read-only).
        [[nodiscard]] std::span<const Image> bands() const { return m_bands; }

        /// @brief Non-owning span over all band images (mutable).
        [[nodiscard]] std::span<Image> bands() { return m_bands; }

        // Iteration helpers (range-for support) -----------------------------

        [[nodiscard]] auto begin()       { return m_bands.begin(); }
        [[nodiscard]] auto end()         { return m_bands.end(); }
        [[nodiscard]] auto begin() const { return m_bands.begin(); }
        [[nodiscard]] auto end()   const { return m_bands.end(); }

    private:
        u32  m_width;
        u32  m_height;
        f64  m_pixel_scale;
        std::vector<Image> m_bands;
    };

} // namespace parallax::imaging

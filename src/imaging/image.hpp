#pragma once

/// @file image.hpp
/// @brief Single-band float image buffer with per-image metadata.
///
/// Stores pixel data in electrons/flux units as a row-major 2D f32 buffer.
/// Metadata carries the band name, center wavelength (nm), and pixel scale
/// (arcsec/px) needed by the PSF and image-formation pipeline.

#include "core/types.hpp"

#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace parallax::imaging
{
    /// @brief Metadata describing a single spectral band image.
    struct ImageMetadata
    {
        std::string band_name;           ///< Human-readable band label (e.g. "V", "H-alpha").
        u32         band_index   = 0;    ///< Index in the parent MultispectralImage.
        f64         wavelength_nm = 550.0; ///< Center wavelength of the band (nm).
        f64         pixel_scale_arcsec_per_px = 1.0; ///< Angular pixel scale (arcsec/px).
    };

    /// @brief 2D float image in electrons/flux units.
    ///
    /// Pixels are stored row-major: pixel(x, y) is at index y*width + x.
    /// All pixel values are initialised to 0.0 on construction.
    class Image
    {
    public:
        /// @brief Construct a zero-initialised image.
        /// @param width  Number of columns (pixels).
        /// @param height Number of rows (pixels).
        /// @param meta   Band metadata associated with this image.
        Image(u32 width, u32 height, ImageMetadata meta = {})
            : m_width(width)
            , m_height(height)
            , m_meta(std::move(meta))
            , m_pixels(static_cast<std::size_t>(width) * height, 0.0f)
        {
        }

        // Accessors ---------------------------------------------------------

        /// @brief Image width in pixels.
        [[nodiscard]] u32 width() const { return m_width; }

        /// @brief Image height in pixels.
        [[nodiscard]] u32 height() const { return m_height; }

        /// @brief Associated band metadata.
        [[nodiscard]] const ImageMetadata& metadata() const { return m_meta; }

        /// @brief Mutable reference to the metadata (e.g. to update pixel scale).
        [[nodiscard]] ImageMetadata& metadata() { return m_meta; }

        // Pixel access (unchecked) ------------------------------------------

        /// @brief Unchecked pixel access (column-major: x across, y down).
        [[nodiscard]] f32  operator()(u32 x, u32 y) const { return m_pixels[index(x, y)]; }
        [[nodiscard]] f32& operator()(u32 x, u32 y)       { return m_pixels[index(x, y)]; }

        // Pixel access (bounds-checked) -------------------------------------

        /// @brief Bounds-checked pixel read. Throws std::out_of_range if outside image.
        [[nodiscard]] f32 at(u32 x, u32 y) const
        {
            check_bounds(x, y);
            return m_pixels[index(x, y)];
        }

        /// @brief Bounds-checked pixel write reference.
        [[nodiscard]] f32& at(u32 x, u32 y)
        {
            check_bounds(x, y);
            return m_pixels[index(x, y)];
        }

        // Bulk access -------------------------------------------------------

        /// @brief Raw pointer to the pixel buffer (row-major).
        [[nodiscard]] const f32* data() const { return m_pixels.data(); }
        [[nodiscard]] f32*       data()       { return m_pixels.data(); }

        /// @brief Non-owning span over all pixels (row-major).
        [[nodiscard]] std::span<const f32> pixels() const { return m_pixels; }
        [[nodiscard]] std::span<f32>       pixels()       { return m_pixels; }

        // Manipulation ------------------------------------------------------

        /// @brief Set every pixel to the given value.
        void fill(f32 value)
        {
            std::fill(m_pixels.begin(), m_pixels.end(), value);
        }

        /// @brief Zero out all pixels (equivalent to fill(0.0f)).
        void clear() { fill(0.0f); }

    private:
        u32           m_width;
        u32           m_height;
        ImageMetadata m_meta;
        std::vector<f32> m_pixels;

        [[nodiscard]] std::size_t index(u32 x, u32 y) const
        {
            return static_cast<std::size_t>(y) * m_width + x;
        }

        void check_bounds(u32 x, u32 y) const
        {
            if (x >= m_width || y >= m_height)
            {
                throw std::out_of_range("Image::at — pixel coordinates out of range");
            }
        }
    };

} // namespace parallax::imaging

#pragma once

/// @file imaging.hpp
/// @brief Gridding and dirty-image formation for the Glasswing Array (Sprint 10b Task 10b.3).
///
/// Converts a set of (u,v) visibility samples into:
///   - The **dirty beam** (point-spread function of the array): IFFT of the sampling weights.
///   - The **dirty image** (aperture synthesis image before deconvolution): IFFT of the
///     gridded visibilities.
///
/// Both outputs are fftshifted (DC/peak at centre pixel N/2,N/2) and normalised so that
/// beam[N/2 * N + N/2] == 1.  A point source of flux F then appears as a peak of amplitude
/// F in the dirty image (and a peak of amplitude F in the restored image after CLEAN).
///
/// @note Requires `src/core/fft.hpp` (radix-2 FFT, 2-D IFFT, shift2).

#include "interferometry/uv_sampling.hpp"

#include <cstdint>
#include <vector>

namespace parallax::interferometry
{
    // ── Visibility weighting ──────────────────────────────────────────────────

    enum class Weighting
    {
        Natural,  ///< No per-cell normalisation — maximises sensitivity.
        Uniform,  ///< Divide each occupied cell by its sample count — improves resolution.
    };

    // ── Dirty images output ───────────────────────────────────────────────────

    /// Gridded interferometric dirty images.
    ///
    /// Both `beam` and `dirty` are N×N row-major vectors of `double`.
    /// Elements are indexed as `data[row * N + col]`.
    /// After `make_images` both arrays are fftshifted (DC at N/2, N/2) and
    /// normalised by the beam peak so that beam[N/2 * N + N/2] == 1.
    struct DirtyImages
    {
        std::vector<double> beam;  ///< Dirty beam (PSF), beam-peak normalised.
        std::vector<double> dirty; ///< Dirty image, beam-peak normalised.
        std::uint32_t N;           ///< Grid side length.
        double du;                 ///< uv cell size (wavelengths per pixel; = 1/theta_fov_rad).
    };

    /// Grid the visibility samples and form the dirty beam and dirty image.
    ///
    /// @param points       Visibility samples from sample_uv().
    /// @param du           uv-grid cell size (wavelengths/pixel = 1/theta_fov_rad).
    /// @param N            Grid side length (must be a power of 2).
    /// @param weighting    Natural or Uniform.
    /// @return DirtyImages with beam-peak-normalised beam and dirty image.
    [[nodiscard]] DirtyImages make_images(const std::vector<Visibility>& points,
                                          double du,
                                          std::uint32_t N,
                                          Weighting weighting);

} // namespace parallax::interferometry

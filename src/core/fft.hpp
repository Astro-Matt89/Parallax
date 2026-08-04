#pragma once

/// @file fft.hpp
/// @brief Radix-2 Cooley-Tukey FFT utilities for the Parallax interferometry pipeline.
///
/// Provides 1-D complex FFT (in-place), 2-D FFT via row-column decomposition,
/// and fftshift.  All sizes must be powers of 2.
///
/// Normalization convention
/// ────────────────────────
/// The *forward* FFT (fft1d / fft2) is un-normalised (no 1/N factor).
/// The *inverse* FFT (ifft1d / ifft2) divides by N (1-D) or N² (2-D).
/// This matches the sandbox convention: IFFT(FFT(x)) == x, and the zero-
/// frequency bin of IFFT(W) equals the sum of all weights divided by N².
///
/// @note Only power-of-2 sizes are supported.  An assertion fires for other sizes.

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace parallax::core
{
    /// Verify that n is a power of 2 (n > 0).
    [[nodiscard]] inline bool is_power_of_two(std::uint32_t n) noexcept
    {
        return n > 0u && (n & (n - 1u)) == 0u;
    }

    /// In-place radix-2 DIT Cooley-Tukey FFT.
    /// @param data  N complex samples (will be overwritten with the DFT).
    /// @param inverse  If true, compute the inverse DFT (with 1/N normalisation).
    void fft1d(std::vector<std::complex<double>>& data, bool inverse);

    /// 2-D forward FFT (row-column decomposition).
    /// @param re  Real part, N×N row-major (index = row*N + col).
    /// @param im  Imaginary part, same layout.
    /// @param N   Grid side length (must be a power of 2).
    void fft2(std::vector<double>& re, std::vector<double>& im, std::uint32_t N);

    /// 2-D inverse FFT with 1/N² normalisation.
    void ifft2(std::vector<double>& re, std::vector<double>& im, std::uint32_t N);

    /// In-place 2-D fftshift: swap quadrants so DC is at (N/2, N/2).
    /// Equivalent to numpy.fft.fftshift for 2-D arrays.
    /// @note N must be even (power-of-2 grids satisfy this automatically).
    void shift2(std::vector<double>& data, std::uint32_t N);

} // namespace parallax::core

#pragma once

/// @file healpix_nested.hpp
/// @brief Minimal vendored HEALPix nested-scheme implementation (header-only).
///
/// Algorithm source:
///   Górski, K.M. et al. (2005), "HEALPix: A Framework for High-Resolution
///   Discretization and Fast Analysis of Data Distributed on the Sphere",
///   ApJ 622, 759–771.  https://doi.org/10.1086/427976
///
/// Formulas taken directly from Section 4 of the paper.  No GPL/LGPL code
/// was copied.  Written from scratch using the paper equations.
///
/// License: BSD 2-Clause.
/// Copyright (c) 2024 Parallax Project Contributors.
/// All rights reserved.
///
/// Redistribution and use in source and binary forms, with or without
/// modification, are permitted provided that the following conditions are met:
/// 1. Redistributions of source code must retain the above copyright notice,
///    this list of conditions and the following disclaimer.
/// 2. Redistributions in binary form must reproduce the above copyright notice,
///    this list of conditions and the following disclaimer in the documentation
///    and/or other materials provided with the distribution.
///
/// THIS SOFTWARE IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND.

#include <cstdint>
#include <cmath>
#include <vector>

namespace parallax::universe::healpix
{

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace detail
{
    /// @brief Per-face ring-start index (jrll), Górski 2005 Table 1.
    /// For face f: ring index at ix=0,iy=0 equals jrll[f] * nside.
    /// Faces 0–3: north polar cap faces (jrll=2)
    /// Faces 4–7: equatorial belt faces (jrll=3)
    /// Faces 8–11: south polar cap faces (jrll=4)
    inline constexpr int kJrll[12] = { 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4 };

    /// @brief Per-face phi offset table (jpll), Górski 2005 Table 1.
    inline constexpr int kJpll[12] = { 1, 3, 5, 7, 0, 2, 4, 6, 1, 3, 5, 7 };

    /// @brief Spread bits of `x` to even-bit positions (0, 2, 4, …) of the result.
    ///
    /// bit k of x → bit 2k of result.  Used to encode (ix, iy) → nested sub-index.
    [[nodiscard]] constexpr std::int64_t spread_bits(std::int64_t x) noexcept
    {
        x &= 0x00000000FFFFFFFFll;
        x = (x ^ (x << 16)) & 0x0000FFFF0000FFFFll;
        x = (x ^ (x <<  8)) & 0x00FF00FF00FF00FFll;
        x = (x ^ (x <<  4)) & 0x0F0F0F0F0F0F0F0Fll;
        x = (x ^ (x <<  2)) & 0x3333333333333333ll;
        x = (x ^ (x <<  1)) & 0x5555555555555555ll;
        return x;
    }

    /// @brief Compress even-bit positions of `x` to a contiguous value.
    ///
    /// Inverse of spread_bits: bit 2k of x → bit k of result.
    [[nodiscard]] constexpr std::int64_t compress_bits(std::int64_t x) noexcept
    {
        x &= 0x5555555555555555ll;
        x = (x ^ (x >>  1)) & 0x3333333333333333ll;
        x = (x ^ (x >>  2)) & 0x0F0F0F0F0F0F0F0Fll;
        x = (x ^ (x >>  4)) & 0x00FF00FF00FF00FFll;
        x = (x ^ (x >>  8)) & 0x0000FFFF0000FFFFll;
        x = (x ^ (x >> 16)) & 0x00000000FFFFFFFFll;
        return x;
    }

    /// @brief Encode local pixel coordinates (ix, iy) to a nested sub-index within a face.
    [[nodiscard]] constexpr std::int64_t xyf_to_ipf(std::int64_t ix, std::int64_t iy) noexcept
    {
        return spread_bits(ix) | (spread_bits(iy) << 1);
    }

    /// @brief Decode a nested sub-index (ipf) to local pixel coordinates (ix, iy).
    constexpr void ipf_to_xyf(std::int64_t ipf, std::int64_t& ix, std::int64_t& iy) noexcept
    {
        ix = compress_bits(ipf);
        iy = compress_bits(ipf >> 1);
    }
} // namespace detail

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/// @brief Convert sky angles to the nested HEALPix pixel index.
///
/// Implements the ang2pix_nest algorithm from Górski et al. 2005, §4.1.
///
/// @param nside      HEALPix resolution parameter (power of 2, ≥ 1).
/// @param theta_rad  Colatitude in radians (0 = north pole, π = south pole).
/// @param phi_rad    Longitude in radians (0..2π).
/// @return           Nested pixel index in [0, 12*nside²).
[[nodiscard]] inline std::int64_t ang2pix_nest(std::int64_t nside,
                                                double       theta_rad,
                                                double       phi_rad) noexcept
{
    constexpr double kTwoPi = 6.283185307179586;
    constexpr double kTwoThirds = 2.0 / 3.0;

    // Normalise phi to [0, 2π)
    phi_rad = std::fmod(phi_rad, kTwoPi);
    if (phi_rad < 0.0) { phi_rad += kTwoPi; }

    const double z  = std::cos(theta_rad);
    const double za = std::abs(z);
    const double tp = phi_rad / kTwoPi; // in [0, 1)

    std::int64_t face_num;
    std::int64_t ix, iy;

    if (za <= kTwoThirds)
    {
        // -----------------------------------------------------------------------
        // Equatorial region (Górski 2005, eq. 5)
        // -----------------------------------------------------------------------
        const double temp1 = static_cast<double>(nside) * (0.5 + tp);
        const double temp2 = static_cast<double>(nside) * z * 0.75;

        const std::int64_t jp = static_cast<std::int64_t>(temp1 - temp2); // ascending edge
        const std::int64_t jm = static_cast<std::int64_t>(temp1 + temp2); // descending edge

        const std::int64_t ifp = jp / nside; // ascending edge face number
        const std::int64_t ifm = jm / nside; // descending edge face number

        if (ifp == ifm)
        {
            face_num = (ifp & 3LL) + 4LL;       // equatorial face
        }
        else if (ifp < ifm)
        {
            face_num = ifp & 3LL;                // north polar face
        }
        else
        {
            face_num = (ifm & 3LL) + 8LL;        // south polar face
        }

        ix = jm % nside;
        iy = nside - (jp % nside) - 1;
    }
    else
    {
        // -----------------------------------------------------------------------
        // Polar caps (Górski 2005, eq. 4)
        // -----------------------------------------------------------------------
        const double tp_polar        = 1.0 - za;
        // polar_coord_scale: scales phi to a ring-edge crossing index in the polar cap.
        // Equivalent to nside * sqrt(3*(1-|z|)), where 1-|z| = tp_polar.
        const double polar_coord_scale = static_cast<double>(nside) * std::sqrt(3.0 * tp_polar);

        // Precompute phi / (π/2) once to avoid redundant division
        const double phi_over_halfpi   = phi_rad / (kTwoPi / 4.0);

        const std::int64_t jp = static_cast<std::int64_t>(phi_over_halfpi * polar_coord_scale);
        const std::int64_t jm = static_cast<std::int64_t>(phi_over_halfpi * polar_coord_scale) + nside;

        // Clamp to valid range
        const std::int64_t jp_c = std::max(std::int64_t{0}, jp);
        const std::int64_t jm_c = std::max(std::int64_t{0}, jm);

        if (z >= 0.0)
        {
            face_num = jp_c % 4LL;
        }
        else
        {
            face_num = 8LL + (jm_c % 4LL);
        }

        // Local ix, iy within the polar cap face
        ix = jm_c % nside;
        iy = jp_c % nside;

        // Clamp to [0, nside)
        if (ix >= nside) { ix = nside - 1; }
        if (iy >= nside) { iy = nside - 1; }
        if (ix < 0)      { ix = 0; }
        if (iy < 0)      { iy = 0; }
    }

    const std::int64_t ipf = detail::xyf_to_ipf(ix, iy);
    return face_num * nside * nside + ipf;
}

/// @brief Convert a nested HEALPix pixel index to sky angles (pixel center).
///
/// Implements the pix2ang_nest algorithm from Górski et al. 2005, §4.1.
///
/// @param nside      HEALPix resolution parameter (power of 2, ≥ 1).
/// @param ipix       Nested pixel index in [0, 12*nside²).
/// @param theta_rad  Output colatitude (0 = north pole, π = south pole).
/// @param phi_rad    Output longitude [0, 2π).
inline void pix2ang_nest(std::int64_t nside, std::int64_t ipix,
                          double& theta_rad, double& phi_rad) noexcept
{
    constexpr double kTwoPi   = 6.283185307179586;
    constexpr double kHalfPi  = 1.5707963267948966;

    const std::int64_t npix = 12LL * nside * nside;
    if (ipix < 0 || ipix >= npix)
    {
        theta_rad = 0.0;
        phi_rad   = 0.0;
        return;
    }

    const std::int64_t face_num = ipix / (nside * nside);
    const std::int64_t ipf      = ipix % (nside * nside);

    std::int64_t ix, iy;
    detail::ipf_to_xyf(ipf, ix, iy);

    // Reduced ring index jr = jrll[face] * nside - ix - iy - 1
    // Ranges: north polar cap faces → jr ∈ [1, 2*nside-1]
    //         equatorial belt faces → jr ∈ [nside, 3*nside]
    //         south polar cap faces → jr ∈ [3*nside+1, 4*nside-1]
    const std::int64_t jr = static_cast<std::int64_t>(detail::kJrll[face_num]) * nside
                           - ix - iy - 1LL;

    double z;
    std::int64_t nr;
    std::int64_t kshift;

    if (jr < nside)
    {
        // North polar cap — Górski 2005, eq. (4)
        nr     = jr;
        z      = 1.0 - static_cast<double>(nr * nr)
                     / (3.0 * static_cast<double>(nside * nside));
        kshift = 1;
    }
    else if (jr <= 3LL * nside)
    {
        // Equatorial belt — Górski 2005, eq. (5)
        nr     = nside;
        z      = (2.0 * static_cast<double>(nside) - static_cast<double>(jr))
                * (2.0 / (3.0 * static_cast<double>(nside)));
        kshift = (jr - nside) & 1LL;
    }
    else
    {
        // South polar cap — mirror of north (Górski 2005, eq. (4))
        nr     = 4LL * nside - jr;
        z      = -1.0 + static_cast<double>(nr * nr)
                       / (3.0 * static_cast<double>(nside * nside));
        kshift = 1;
    }

    // Pixel index in the ring
    std::int64_t ip = (static_cast<std::int64_t>(detail::kJpll[face_num]) * nr
                       + ix - iy + 1LL + kshift) / 2LL - 1LL;
    const std::int64_t ring_len = 4LL * nr;
    ip = ((ip % ring_len) + ring_len) % ring_len; // wrap to [0, 4*nr)

    phi_rad   = (static_cast<double>(ip) + 0.5 * static_cast<double>(kshift))
              * kHalfPi / static_cast<double>(nr);
    theta_rad = std::acos(std::max(-1.0, std::min(1.0, z)));

    // Normalise phi to [0, 2π)
    if (phi_rad < 0.0)    { phi_rad += kTwoPi; }
    if (phi_rad >= kTwoPi) { phi_rad -= kTwoPi; }
}

/// @brief Find all nested pixels that overlap a disc (inclusive approximation).
///
/// Inclusive semantics: over-includes rather than under-includes — a pixel is
/// included when its center is within @p radius_rad + half_pixel_diagonal of the
/// query point.  Adequate for visual rendering; no strict mathematical optimality
/// guarantee.
///
/// Implementation: brute-force test of every pixel center against the effective
/// disc. For nside = 64 (49152 pixels) this is fast enough (≪ 1 ms) because
/// each test is a simple dot-product on unit vectors.
///
/// @param nside       HEALPix resolution parameter.
/// @param theta_rad   Query-disc centre colatitude (radians).
/// @param phi_rad     Query-disc centre longitude (radians).
/// @param radius_rad  Half-angle of the query disc (radians).
/// @param pixels      Output vector — matching pixel IDs are appended.
inline void query_disc_inclusive_nest(std::int64_t               nside,
                                       double                     theta_rad,
                                       double                     phi_rad,
                                       double                     radius_rad,
                                       std::vector<std::int64_t>& pixels)
{
    constexpr double kPi = 3.141592653589793;

    // Half-diagonal of a pixel: half_side * sqrt(2).
    // pixel area = 4π/(12*nside²) sr; pixel side ≈ sqrt(area).
    const double pixel_area_sr  = 4.0 * kPi / (12.0 * static_cast<double>(nside * nside));
    const double half_diag_rad  = 0.5 * std::sqrt(2.0 * pixel_area_sr);
    const double effective_cos  = std::cos(radius_rad + half_diag_rad);

    // Query centre as unit vector
    const double sin_t0 = std::sin(theta_rad);
    const double cx = sin_t0 * std::cos(phi_rad);
    const double cy = sin_t0 * std::sin(phi_rad);
    const double cz = std::cos(theta_rad);

    const std::int64_t npix = 12LL * nside * nside;
    for (std::int64_t ipix = 0; ipix < npix; ++ipix)
    {
        double t, p;
        pix2ang_nest(nside, ipix, t, p);

        const double st  = std::sin(t);
        const double dot = cx * (st * std::cos(p))
                         + cy * (st * std::sin(p))
                         + cz * std::cos(t);

        if (dot >= effective_cos)
        {
            pixels.push_back(ipix);
        }
    }
}

} // namespace parallax::universe::healpix

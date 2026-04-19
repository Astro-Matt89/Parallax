/// @file procedural_provider.cpp
/// @brief Implementation of ProceduralProvider.
///
/// Performance notes:
///   - query_fov steady-state (warm cache) target: < 2 ms for 30° FOV, mag ≤ 14.
///   - Cache hit rate is logged at DEBUG level (throttled to once per ~5 s).
///   - Cell generation time is logged at DEBUG level per cell.
///   - No SIMD or threading — single-threaded generation is adequate at this scale.
///   - The dominant cost is query_disc_inclusive_nest (O(12*nside²) = O(49152) dot
///     products) which takes < 0.5 ms on typical hardware.

#include "universe/procedural_provider.hpp"

#include "universe/healpix/healpix_nested.hpp"
#include "universe/rng/splitmix64.hpp"
#include "universe/rng/xoshiro256ss.hpp"
#include "core/logger.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <numbers>
#include <numeric>

namespace parallax::universe
{

// =============================================================================
// Anonymous helpers
// =============================================================================

namespace
{
    // -------------------------------------------------------------------------
    // Physical constants
    // -------------------------------------------------------------------------

    constexpr double kPi     = std::numbers::pi;
    constexpr double kTwoPi  = 2.0 * kPi;
    constexpr double kDegToRad = kPi / 180.0;
    constexpr double kRadToDeg = 180.0 / kPi;

    // -------------------------------------------------------------------------
    // Density model constants (Górski galactic-latitude weight)
    // -------------------------------------------------------------------------

    /// @brief Base star density at the galactic poles (stars per deg²) up to mag ≤ 14.
    static constexpr double kBaseDensityPerSqDeg = 500.0;

    /// @brief Multiplicative boost at the galactic plane relative to poles.
    static constexpr double kDiskFactor = 5.0;

    /// @brief Scale height of the galactic disk (degrees).
    static constexpr double kScaleHeightDeg = 10.0;

    // -------------------------------------------------------------------------
    // Galactic-coordinate transform (J2000)
    // -------------------------------------------------------------------------

    /// @brief Convert J2000 equatorial (ra, dec) to galactic (l, b).
    ///
    /// Uses the IAU J2000 rotation constants:
    ///   North Galactic Pole: RA = 192.85948°, Dec = +27.12825°
    ///   Position angle of galactic north at the NGP: theta = 122.93192°
    ///   (Galactic longitude of ascending node = 32.93192°, from NCP)
    ///
    /// Reference: Liu, J.C. et al. (2011), "The Galactic Coordinate System"
    /// and the standard IAU 1958 / J2000 definition.
    ///
    /// The 3×3 rotation matrix R below is pre-computed from these values:
    ///   R = Rz(180° - 122.93192°) · Ry(90° - 27.12825°) · Rz(192.85948°)
    /// (rotation from equatorial to galactic frame)
    void equatorial_to_galactic(double ra_rad, double dec_rad,
                                 double& l_rad, double& b_rad) noexcept
    {
        // J2000 galactic rotation matrix (row-major, accurate to ~6 decimal places).
        // Source: IAU 1958 galactic frame applied to J2000 equator.
        // Liu et al. 2011 / standard IAU values.
        static constexpr double R[3][3] =
        {
            { -0.054875539, -0.873437105, -0.483835039 },
            {  0.494109454, -0.444829594,  0.746982248 },
            { -0.867666136, -0.198076390,  0.455983776 },
        };

        const double cd = std::cos(dec_rad);
        const double sd = std::sin(dec_rad);
        const double cr = std::cos(ra_rad);
        const double sr = std::sin(ra_rad);

        // Equatorial unit vector
        const double x = cd * cr;
        const double y = cd * sr;
        const double z = sd;

        // Galactic unit vector = R * [x, y, z]^T
        const double gx = R[0][0] * x + R[0][1] * y + R[0][2] * z;
        const double gy = R[1][0] * x + R[1][1] * y + R[1][2] * z;
        const double gz = R[2][0] * x + R[2][1] * y + R[2][2] * z;

        b_rad = std::asin(std::max(-1.0, std::min(1.0, gz)));
        l_rad = std::atan2(gy, gx);
        if (l_rad < 0.0) { l_rad += kTwoPi; }
    }

    // -------------------------------------------------------------------------
    // Density model
    // -------------------------------------------------------------------------

    /// @brief Star density (stars per deg²) at galactic latitude @p b_deg.
    ///
    /// density(b) = base_density * (1 + disk_factor * exp(-|b| / scale_height))
    [[nodiscard]] double stellar_density(double b_deg) noexcept
    {
        return kBaseDensityPerSqDeg
             * (1.0 + kDiskFactor * std::exp(-std::abs(b_deg) / kScaleHeightDeg));
    }

    // -------------------------------------------------------------------------
    // Inverse-CDF magnitude sampling
    // -------------------------------------------------------------------------

    /// @brief Sample a magnitude from P(m) ∝ 10^(0.6 m) on [m_min, m_max].
    ///
    /// Analytic inverse CDF:
    ///   m = (1/0.6) * log10(u * (10^(0.6*m_max) - 10^(0.6*m_min)) + 10^(0.6*m_min))
    /// where u is uniform on [0, 1).
    [[nodiscard]] float sample_magnitude(double u, float m_min, float m_max) noexcept
    {
        const double lo  = std::pow(10.0, 0.6 * static_cast<double>(m_min));
        const double hi  = std::pow(10.0, 0.6 * static_cast<double>(m_max));
        const double val = lo + u * (hi - lo);
        const double m   = std::log10(std::max(val, 1e-30)) / 0.6;
        return static_cast<float>(std::max(static_cast<double>(m_min),
                                           std::min(static_cast<double>(m_max), m)));
    }

    // -------------------------------------------------------------------------
    // Cell-seed derivation
    // -------------------------------------------------------------------------

    /// @brief Derive a deterministic cell seed from the master seed and pixel ID.
    [[nodiscard]] std::uint64_t derive_cell_seed(std::uint64_t master_seed,
                                                  std::int64_t  pixel_id) noexcept
    {
        // chain two SplitMix64 hashes for good avalanche
        using rng::splitmix64;
        return splitmix64(master_seed ^ splitmix64(static_cast<std::uint64_t>(pixel_id)));
    }

} // namespace

// =============================================================================
// set_master_seed
// =============================================================================

void ProceduralProvider::set_master_seed(std::uint64_t seed)
{
    std::lock_guard<std::mutex> lock(m_cache_mutex);
    m_master_seed = seed;
    m_cell_cache.clear();
    m_lru_order.clear();
    // pixel_uvecs are independent of the seed (they depend only on nside) — keep them.
    // Reset stats so that hit-rate logging reflects the new seed's session only.
    m_first_query_logged = false;
    m_query_count        = 0;
    m_cache_hits         = 0;
    PLX_CORE_INFO("ProceduralProvider: master seed set to {:#018x}", seed);
}

// =============================================================================
// query_fov
// =============================================================================

void ProceduralProvider::query_fov(double     ra,
                                    double     dec,
                                    double     radius_deg,
                                    float      mag_limit,
                                    QueryFlags flags,
                                    std::vector<CelestialObject>& results) const
{
    if (!has_flag(flags, QueryFlags::Procedural))
    {
        return;
    }

    if (mag_limit <= kMinProceduralMag)
    {
        return;
    }

    // Log on first call
    if (!m_first_query_logged)
    {
        PLX_CORE_INFO("ProceduralProvider: first query — master_seed={:#018x}, "
                      "nside={}, max_cached_cells={}",
                      m_master_seed,
                      static_cast<int>(m_nside),
                      kMaxCachedCells);
        m_first_query_logged = true;
    }

    ++m_query_count;

    // -------------------------------------------------------------------------
    // Precompute pixel centre unit vectors on first use (one-time cost).
    // Avoids O(nside²) pix2ang_nest calls on every subsequent query.
    // -------------------------------------------------------------------------
    if (m_pixel_uvecs.empty())
    {
        const std::int64_t npix = 12LL * m_nside * m_nside;
        m_pixel_uvecs.resize(static_cast<std::size_t>(npix));
        for (std::int64_t i = 0; i < npix; ++i)
        {
            double theta, phi;
            healpix::pix2ang_nest(m_nside, i, theta, phi);
            const float st = static_cast<float>(std::sin(theta));
            m_pixel_uvecs[static_cast<std::size_t>(i)] = {
                st * static_cast<float>(std::cos(phi)),
                st * static_cast<float>(std::sin(phi)),
                static_cast<float>(std::cos(theta))
            };
        }
        PLX_CORE_INFO("ProceduralProvider: precomputed {} pixel centres", npix);
    }

    // -------------------------------------------------------------------------
    // Convert query cone to unit vector + cosine threshold.
    // Using dot-product comparison avoids acos/atan2 in the hot loops.
    // -------------------------------------------------------------------------
    const double radius_rad = radius_deg * kDegToRad;

    const double cos_dec  = std::cos(dec);
    const float  qx       = static_cast<float>(cos_dec * std::cos(ra));
    const float  qy       = static_cast<float>(cos_dec * std::sin(ra));
    const float  qz       = static_cast<float>(std::sin(dec));

    // Effective disc threshold includes half-pixel diagonal for inclusive semantics.
    const double pixel_area_sr  = 4.0 * kPi / (12.0 * static_cast<double>(m_nside * m_nside));
    const double half_diag_rad  = 0.5 * std::sqrt(2.0 * pixel_area_sr);
    const float  cos_disc_eff   = static_cast<float>(std::cos(radius_rad + half_diag_rad));

    // Exact threshold for per-star distance check (no extra margin needed).
    const float cos_radius = static_cast<float>(std::cos(radius_rad));

    // -------------------------------------------------------------------------
    // Find overlapping HEALPix cells via precomputed unit vectors.
    // -------------------------------------------------------------------------
    std::vector<std::int64_t> candidate_pixels;
    candidate_pixels.reserve(512);
    {
        const std::int64_t npix = 12LL * m_nside * m_nside;
        for (std::int64_t i = 0; i < npix; ++i)
        {
            const auto& c   = m_pixel_uvecs[static_cast<std::size_t>(i)];
            const float dot = qx * c[0] + qy * c[1] + qz * c[2];
            if (dot >= cos_disc_eff)
            {
                candidate_pixels.push_back(i);
            }
        }
    }

    // -------------------------------------------------------------------------
    // For each candidate cell: ensure generated, filter stars by position + mag.
    // -------------------------------------------------------------------------
    // Periodic cache-hit-rate logging (throttled to once per ~100 queries)
    const bool log_stats = (m_query_count % 100 == 0);

    for (const std::int64_t pixel_id : candidate_pixels)
    {
        ensure_cell_generated(pixel_id);

        std::lock_guard<std::mutex> lock(m_cache_mutex);
        auto it = m_cell_cache.find(pixel_id);
        if (it == m_cell_cache.end())
        {
            continue;
        }

        // Move this cell to the front of the LRU list
        m_lru_order.erase(it->second.lru_it);
        m_lru_order.push_front(pixel_id);
        it->second.lru_it = m_lru_order.begin();

        const std::vector<CelestialObject>&      stars  = it->second.stars;
        const std::vector<std::array<float, 3>>& uvecs  = it->second.star_uvecs;

        // Stars are sorted by mag_v ascending — binary search for the cutoff.
        // This avoids iterating the ~94% of faint stars that don't pass mag_limit.
        // upper_bound gives first element > mag_limit → end of valid range.
        const std::size_t n_valid = static_cast<std::size_t>(
            std::upper_bound(stars.begin(), stars.end(), mag_limit,
                [](float limit, const CelestialObject& s) { return limit < s.mag_v; })
            - stars.begin());

        for (std::size_t j = 0; j < n_valid; ++j)
        {
            // Fast dot-product distance check — avoids trig entirely in the hot path.
            const auto& u   = uvecs[j];
            const float dot = qx * u[0] + qy * u[1] + qz * u[2];
            if (dot >= cos_radius)
            {
                results.push_back(stars[j]);
            }
        }
    }

    if (log_stats)
    {
        const double hit_rate = (m_query_count > 0)
            ? static_cast<double>(m_cache_hits) / static_cast<double>(m_query_count) * 100.0
            : 0.0;

        std::size_t cached_count = 0;
        {
            std::lock_guard<std::mutex> lock(m_cache_mutex);
            cached_count = m_cell_cache.size();
        }

        PLX_CORE_TRACE("ProceduralProvider: queries={} cache_cells={} hit_rate={:.1f}%",
                       m_query_count, cached_count, hit_rate);
    }
}

// =============================================================================
// query_object
// =============================================================================

std::optional<CelestialObject> ProceduralProvider::query_object(u64 id) const
{
    if (decode_type(id) != ObjectType::ProceduralStar)
    {
        return std::nullopt;
    }

    // TODO: implement reverse-lookup cache: (pixel_id, star_index) → CelestialObject.
    //       Currently nothing in the codebase calls query_object on procedural IDs.
    //       When selection/detail panels need this, add an unordered_map<u64, CelestialObject>
    //       populated during generate_cell().
    return std::nullopt;
}

// =============================================================================
// get_count
// =============================================================================

std::size_t ProceduralProvider::get_count() const
{
    // Returns the total star count across all currently cached cells.
    // This is a "warm cache estimate" — not the total possible procedural star
    // count, which is effectively infinite.
    std::lock_guard<std::mutex> lock(m_cache_mutex);
    std::size_t total = 0;
    for (const auto& [pixel_id, cell] : m_cell_cache)
    {
        total += cell.stars.size();
    }
    return total;
}

// =============================================================================
// ensure_cell_generated (private)
// =============================================================================

void ProceduralProvider::ensure_cell_generated(std::int64_t pixel_id) const
{
    {
        std::lock_guard<std::mutex> lock(m_cache_mutex);
        if (m_cell_cache.count(pixel_id) > 0)
        {
            ++m_cache_hits;
            return;
        }
    }

    // Generate outside the lock — generation is pure and deterministic.
    // Two threads may generate the same cell simultaneously; the second
    // write will simply overwrite with an identical result (correctness OK).
    const auto t0 = std::chrono::steady_clock::now();
    CellData data = generate_cell(pixel_id);
    const auto t1 = std::chrono::steady_clock::now();

    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    PLX_CORE_TRACE("ProceduralProvider: generated cell {} with {} stars in {:.3f} ms",
                   pixel_id, data.stars.size(), ms);

    {
        std::lock_guard<std::mutex> lock(m_cache_mutex);
        // Another thread may have inserted this cell while we were generating.
        // Insert only if still absent to avoid losing the LRU iterator.
        if (m_cell_cache.count(pixel_id) == 0)
        {
            evict_lru_if_needed();
            m_lru_order.push_front(pixel_id);
            data.lru_it = m_lru_order.begin();
            m_cell_cache.emplace(pixel_id, std::move(data));
        }
    }
}

// =============================================================================
// generate_cell (private)
// =============================================================================

ProceduralProvider::CellData ProceduralProvider::generate_cell(std::int64_t pixel_id) const
{
    CellData cell;

    // ------------------------------------------------------------------
    // Step 1: Derive the cell seed (deterministic, no external state)
    // ------------------------------------------------------------------
    const std::uint64_t cell_seed = derive_cell_seed(m_master_seed, pixel_id);

    // ------------------------------------------------------------------
    // Step 2: Seed the per-cell RNG
    // ------------------------------------------------------------------
    rng::Xoshiro256ss rng{cell_seed};

    // ------------------------------------------------------------------
    // Step 3: Compute cell centre (ra, dec) from HEALPix pixel
    // theta = pi/2 - dec, phi = ra
    // ------------------------------------------------------------------
    double theta_c, phi_c;
    healpix::pix2ang_nest(m_nside, pixel_id, theta_c, phi_c);
    const double ra_c  = phi_c;
    const double dec_c = kPi / 2.0 - theta_c;

    // ------------------------------------------------------------------
    // Step 4: Galactic latitude for density model
    // ------------------------------------------------------------------
    double l_rad, b_rad;
    equatorial_to_galactic(ra_c, dec_c, l_rad, b_rad);
    const double b_deg = b_rad * kRadToDeg;

    // ------------------------------------------------------------------
    // Step 5: Star count (Poisson approximation)
    // ------------------------------------------------------------------
    // Cell area = 4π / (12 * nside²) steradians → convert to deg²
    // Conversion factor: (180/π)² ≈ 3282.8 deg²/sr  (kRadToDeg² is correct here)
    const double cell_area_sr     = 4.0 * kPi / (12.0 * static_cast<double>(m_nside * m_nside));
    const double cell_area_deg_sq = cell_area_sr * kRadToDeg * kRadToDeg;

    const double density        = stellar_density(b_deg);
    const double expected_count = density * cell_area_deg_sq;

    // Simple Poisson approximation: round(expected + uniform(-0.5, 0.5))
    // next_double() returns [0, 1), so subtracting 0.5 gives [-0.5, 0.5).
    const double u_count   = rng.next_double() - 0.5;
    const auto star_count  = static_cast<std::int64_t>(
        std::max(0.0, std::round(expected_count + u_count)));

    cell.stars.reserve(static_cast<std::size_t>(star_count));

    // ------------------------------------------------------------------
    // Half-diagonal of the cell for rejection-sampling cap
    // For a pixel of area A sr, the bounding circle has radius ≈ sqrt(A/2)
    // because the half-diagonal of a square is side/sqrt(2) = sqrt(A)/sqrt(2).
    // std::numbers::inv_sqrt2 = 1/sqrt(2) ≈ 0.70710678.
    // ------------------------------------------------------------------
    const double pixel_area_sr  = cell_area_sr;
    const double half_diag_rad  = std::sqrt(pixel_area_sr) / std::numbers::sqrt2;

    // ------------------------------------------------------------------
    // Step 6–8: Generate each star
    // ------------------------------------------------------------------
    for (std::int64_t i = 0; i < star_count; ++i)
    {
        // ----------------------------------------------------------------
        // 6a. Position: rejection-sample within the cell's bounding cap.
        //     Generate a point within the cell's bounding cap
        //     (uniform on the spherical cap of half_diag_rad around the cell centre)
        //     and accept only if ang2pix_nest maps it back to pixel_id.
        //     Cap retries at 20; accept unconditionally after that.
        // ----------------------------------------------------------------
        double star_ra  = ra_c;
        double star_dec = dec_c;

        constexpr int kMaxRetries = 20;
        for (int attempt = 0; attempt < kMaxRetries; ++attempt)
        {
            // Sample a random direction within a cap of half_diag_rad around (theta_c, phi_c)
            // Using the standard spherical cap uniform sampling method:
            //   cos_angle = 1 - u * (1 - cos(half_diag_rad))
            const double u1 = rng.next_double();
            const double u2 = rng.next_double();

            const double cos_cap = std::cos(half_diag_rad);
            const double cos_z   = 1.0 - u1 * (1.0 - cos_cap);
            const double sin_z   = std::sqrt(std::max(0.0, 1.0 - cos_z * cos_z));
            const double psi     = kTwoPi * u2;

            // Random point relative to the cap axis (north pole alignment)
            const double rx_local = sin_z * std::cos(psi);
            const double ry_local = sin_z * std::sin(psi);
            const double rz_local = cos_z;

            // Rotate to align with the cap centre (theta_c, phi_c) using
            // R = Rz(phi_c) * Ry(theta_c)  — maps z-axis to the cap-centre direction.
            // cth = cos(theta_c), sth = sin(theta_c) where theta_c = pi/2 - dec_c.
            const double cth = std::cos(theta_c);  // = sin(dec_c)
            const double sth = std::sin(theta_c);  // = cos(dec_c)
            const double cph = std::cos(phi_c);    // = cos(ra_c)
            const double sph = std::sin(phi_c);    // = sin(ra_c)

            // Standard R = Rz(phi_c) * Ry(theta_c) applied to local vector:
            const double rx_eq = cth * cph * rx_local - sph * ry_local + sth * cph * rz_local;
            const double ry_eq = cth * sph * rx_local + cph * ry_local + sth * sph * rz_local;
            const double rz_eq = -sth * rx_local                       + cth * rz_local;

            const double sample_dec = std::asin(std::max(-1.0, std::min(1.0, rz_eq)));
            double       sample_ra  = std::atan2(ry_eq, rx_eq);
            if (sample_ra < 0.0) { sample_ra += kTwoPi; }

            // Verify this point maps back to our pixel
            const double sample_theta = kPi / 2.0 - sample_dec;
            const std::int64_t check_pix = healpix::ang2pix_nest(m_nside, sample_theta, sample_ra);

            if (check_pix == pixel_id || attempt == kMaxRetries - 1)
            {
                star_ra  = sample_ra;
                star_dec = sample_dec;
                break;
            }
            // else: retry with new random numbers
        }

        // ----------------------------------------------------------------
        // 6b. Magnitude: inverse-CDF sample from P(m) ∝ 10^(0.6 m)
        // ----------------------------------------------------------------
        const double u_mag = rng.next_double();
        const float  mag   = sample_magnitude(u_mag,
                                              kMinProceduralMag,
                                              kMaxGenerationMag);

        // ----------------------------------------------------------------
        // 6c. Color (B-V): triangular distribution on [-0.3, 2.0], mode ~0.8.
        //
        // B-V = 0.4 + (u1 + u2 - 1.0) * 1.1   (triangular, clamped)
        // + weak magnitude correlation: fainter → slightly redder.
        //
        // Not physically perfect — good enough for visual realism.
        // ----------------------------------------------------------------
        const float u_c1 = rng.next_float();
        const float u_c2 = rng.next_float();
        float bv = 0.4f + (u_c1 + u_c2 - 1.0f) * 1.1f;
        bv += (mag - kMinProceduralMag) * 0.04f; // fainter → slightly redder
        bv  = std::max(-0.3f, std::min(2.0f, bv));

        // ----------------------------------------------------------------
        // 6d. ID encoding
        // ----------------------------------------------------------------
        // source_id: hash of (pixel_id, star_index) — lower 56 bits
        using rng::splitmix64;
        const std::uint64_t source_id =
            splitmix64((static_cast<std::uint64_t>(pixel_id) << 20)
                       ^ static_cast<std::uint64_t>(i))
            & UINT64_C(0x00FFFFFFFFFFFFFF); // lower 56 bits

        // ----------------------------------------------------------------
        // 6e. Populate CelestialObject
        // ----------------------------------------------------------------
        CelestialObject obj;
        obj.type     = ObjectType::ProceduralStar;
        obj.id       = encode_id(ObjectType::ProceduralStar, source_id);
        obj.ra       = star_ra;  // radians, J2000
        obj.dec      = star_dec; // radians, J2000
        obj.mag_v    = mag;
        obj.color_bv = bv;

        // StarData fields: all zero for procedural stars
        // (no distance, proper motion, parallax, HIP, or HD number)
        obj.data = StarData{};

        // Precompute the unit vector for fast dot-product distance checks in query_fov.
        const float cos_dec_s = static_cast<float>(std::cos(star_dec));
        const std::array<float, 3> uvec = {
            cos_dec_s * static_cast<float>(std::cos(star_ra)),
            cos_dec_s * static_cast<float>(std::sin(star_ra)),
            static_cast<float>(std::sin(star_dec))
        };

        cell.stars.push_back(std::move(obj));
        cell.star_uvecs.push_back(uvec);
    }

    // Sort stars by magnitude ascending so query_fov can binary-search the
    // mag_limit boundary and skip the faint tail without iterating it.
    // (Faint stars dominate by count — ~94% have mag > 14 for kMaxGenerationMag = 16.)
    // We must sort both stars and star_uvecs together to keep them in sync.
    {
        const std::size_t n = cell.stars.size();
        // Build an index array for stable co-sort
        std::vector<std::size_t> idx(n);
        std::iota(idx.begin(), idx.end(), std::size_t{0});
        std::sort(idx.begin(), idx.end(),
            [&](std::size_t a, std::size_t b) noexcept
            {
                return cell.stars[a].mag_v < cell.stars[b].mag_v;
            });

        std::vector<CelestialObject>         sorted_stars(n);
        std::vector<std::array<float, 3>>    sorted_uvecs(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            sorted_stars[i] = std::move(cell.stars[idx[i]]);
            sorted_uvecs[i] = cell.star_uvecs[idx[i]];
        }
        cell.stars     = std::move(sorted_stars);
        cell.star_uvecs = std::move(sorted_uvecs);
    }

    // lru_it will be set by the caller after inserting into the map
    return cell;
}

// =============================================================================
// evict_lru_if_needed (private)
// =============================================================================

void ProceduralProvider::evict_lru_if_needed() const
{
    // Caller must hold m_cache_mutex.
    while (m_cell_cache.size() >= kMaxCachedCells && !m_lru_order.empty())
    {
        const std::int64_t lru_pixel = m_lru_order.back();
        m_lru_order.pop_back();
        m_cell_cache.erase(lru_pixel);
    }
}

} // namespace parallax::universe

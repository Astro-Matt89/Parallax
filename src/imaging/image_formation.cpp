/// @file image_formation.cpp
/// @brief Image formation engine — Sprint 10a total-power mode.
///
/// Physics summary
/// ───────────────
/// Flux → electrons per second (photon-counting formula):
///   S [e⁻/s] = f_ν [Jy] × 1e-26  ×  (Δλ/λ)  ×  A [m²]  ×  η  /  h
///
///   where:
///     f_ν      — spectral flux density from SNRCalculator::magnitude_to_flux_jy (Jy, AB system)
///     1e-26    — Jy → W/m²/Hz conversion
///     Δλ/λ     — fractional bandwidth (dimensionless; c cancels out)
///     A        — total active collecting area (m²)
///     η        — area-weighted mean efficiency (dimensionless)
///     h        — Planck constant, 6.626e-34 J·s
///
/// Per-band electron count deposited for one object over the integration:
///   electrons = S × t_int
///
/// PSF FWHM in pixels (per band, diffraction limit of the largest single aperture):
///   FWHM_px = Psf::fwhm_px(lambda_nm, D_largest_m, pixel_scale_arcsec_per_px)
///
/// Sky background (flat, same for every pixel in a band):
///   bg_electrons = sky_background_e_per_s_px × t_int
///
/// Per-pixel noise model:
///   1. Poisson sample from (source_signal_px + bg_electrons)
///   2. Add Gaussian read noise N(0, σ_read)
///
/// Projection: gnomonic (tangent-plane) centred on (ra0, dec0)
///   cos_c = sin(dec0)·sin(dec) + cos(dec0)·cos(dec)·cos(Δra)
///   x_rad =  cos(dec)·sin(Δra) / cos_c
///   y_rad = (cos(dec0)·sin(dec) − sin(dec0)·cos(dec)·cos(Δra)) / cos_c
///   x_px  = (width−1)/2  + x_arcsec / pixel_scale   (East → +x)
///   y_px  = (height−1)/2 − y_arcsec / pixel_scale   (North → −y)

#include "imaging/image_formation.hpp"
#include "imaging/psf.hpp"
#include "instruments/snr_calculator.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <cmath>
#include <random>

namespace parallax::imaging
{
    namespace
    {
        using astro_constants::kPi;
        using astro_constants::kArcSecToRad;

        /// Planck constant (J·s).
        constexpr f64 kPlanck = 6.62607015e-34;

        /// Jansky → SI spectral flux density (W/m²/Hz).
        constexpr f64 kJyToSI = 1.0e-26;

        /// Arcseconds per radian (= 180 × 3600 / π).
        constexpr f64 kArcSecPerRad = 1.0 / kArcSecToRad;

        // -------------------------------------------------------------------------
        // Helpers
        // -------------------------------------------------------------------------

        /// @brief Compute the area-weighted mean efficiency of active stations.
        [[nodiscard]] f64 compute_mean_efficiency(
            const instruments::ArrayInstrument& instrument)
        {
            f64 weighted_eff = 0.0;
            f64 total_area   = 0.0;
            for (const auto& station : instrument.get_stations())
            {
                if (!station.is_active) { continue; }
                const f64 r    = static_cast<f64>(station.aperture_diameter_m) * 0.5;
                const f64 area = kPi * r * r;
                weighted_eff  += static_cast<f64>(station.efficiency) * area;
                total_area    += area;
            }
            if (total_area <= 0.0) { return 0.0; }
            return weighted_eff / total_area;
        }

        /// @brief Gnomonic (tangent-plane) projection of a sky position to detector pixels.
        ///
        /// Returns the sub-pixel (x, y) position in detector coordinates where the origin
        /// is at the top-left corner, x increases East, and y increases South (down).
        /// Returns {-1e9, -1e9} when the point is on the wrong side of the tangent plane.
        ///
        /// @param ra_obj   Object right ascension (radians, J2000).
        /// @param dec_obj  Object declination (radians, J2000).
        /// @param ra0      Pointing centre RA (radians).
        /// @param dec0     Pointing centre Dec (radians).
        /// @param scale    Pixel scale (arcsec/px).
        /// @param width    Detector width (pixels).
        /// @param height   Detector height (pixels).
        [[nodiscard]] std::pair<f64, f64> project_gnomonic(
            f64 ra_obj,  f64 dec_obj,
            f64 ra0,     f64 dec0,
            f64 scale,   u32 width, u32 height) noexcept
        {
            const f64 d_ra    = ra_obj - ra0;
            const f64 sin_d0  = std::sin(dec0);
            const f64 cos_d0  = std::cos(dec0);
            const f64 sin_d   = std::sin(dec_obj);
            const f64 cos_d   = std::cos(dec_obj);
            const f64 cos_dra = std::cos(d_ra);

            const f64 cos_c = sin_d0 * sin_d + cos_d0 * cos_d * cos_dra;
            if (cos_c <= 0.0) { return {-1.0e9, -1.0e9}; }

            const f64 x_rad    = cos_d * std::sin(d_ra) / cos_c;
            const f64 y_rad    = (cos_d0 * sin_d - sin_d0 * cos_d * cos_dra) / cos_c;

            const f64 x_arcsec = x_rad * kArcSecPerRad;
            const f64 y_arcsec = y_rad * kArcSecPerRad;

            const f64 cx = (static_cast<f64>(width)  - 1.0) * 0.5;
            const f64 cy = (static_cast<f64>(height) - 1.0) * 0.5;

            // East  → positive x_arcsec → positive x offset from centre.
            // North → positive y_arcsec → negative y offset (y increases downward).
            return { cx + x_arcsec / scale,
                     cy - y_arcsec / scale };
        }

        /// @brief Electron rate for a source in one spectral band.
        ///
        /// S [e⁻/s] = f_ν × kJyToSI × (Δλ/λ) × A × η / h
        [[nodiscard]] f64 compute_electron_rate(
            f64 flux_jy,
            f64 bandwidth_nm,
            f64 center_wavelength_nm,
            f64 collecting_area_m2,
            f64 efficiency) noexcept
        {
            if (center_wavelength_nm <= 0.0 || collecting_area_m2 <= 0.0)
            {
                return 0.0;
            }
            const f64 fractional_bw = bandwidth_nm / center_wavelength_nm;
            return flux_jy * kJyToSI * fractional_bw * collecting_area_m2 * efficiency / kPlanck;
        }

    } // anonymous namespace

    // =========================================================================
    // ImageFormation::form
    // =========================================================================

    MultispectralImage ImageFormation::form(
        const ImageFormationParams&          params,
        const instruments::ArrayInstrument&  instrument,
        const IObjectSource&                 object_source)
    {
        // ---- Derived geometry -----------------------------------------------
        // Pixel scale: the FOV covers width_px columns, so each pixel is
        //   fov_arcsec / width_px arcsec wide.
        const f64 pixel_scale = (params.width_px > 0)
            ? params.fov_arcsec / static_cast<f64>(params.width_px)
            : 1.0;

        // Query radius: half the diagonal of the detector, converted to degrees.
        // This cone guarantees all objects that can overlap the image are returned.
        const f64 half_diag_arcsec = 0.5
            * std::sqrt(static_cast<f64>(params.width_px)  * params.width_px
                      + static_cast<f64>(params.height_px) * params.height_px)
            * pixel_scale;
        const f64 query_radius_deg = half_diag_arcsec / 3600.0;

        // ---- Instrument properties ------------------------------------------
        const f64 collecting_area_m2 = instrument.get_total_collecting_area_m2();
        const f64 largest_aperture_m = instrument.get_largest_aperture_diameter_m();
        const f64 efficiency          = compute_mean_efficiency(instrument);

        // ---- Query objects (shared across all bands) ------------------------
        std::vector<universe::CelestialObject> objects;
        object_source.query_fov(params.ra_rad, params.dec_rad,
                                 query_radius_deg, params.mag_limit, objects);

        // ---- Initialise output image ----------------------------------------
        MultispectralImage result(params.width_px, params.height_px, pixel_scale);

        // One RNG shared across all bands (same seed → same noise per call).
        std::mt19937_64 rng(params.seed);

        // ---- Per-band processing -------------------------------------------
        for (const BandSpec& band : params.bands)
        {
            Image& img = result.emplace_band(ImageMetadata{
                .band_name               = band.band_name,
                .band_index              = band.band_index,
                .wavelength_nm           = band.center_wavelength_nm,
                .pixel_scale_arcsec_per_px = pixel_scale,
            });

            // PSF FWHM in pixels for this band.
            // Falls back to 1 px when there is no active aperture (degenerate case).
            const f64 psf_fwhm_px = (largest_aperture_m > 0.0)
                ? Psf::fwhm_px(band.center_wavelength_nm, largest_aperture_m, pixel_scale)
                : 1.0;

            // -- Render sources -----------------------------------------------
            if (collecting_area_m2 > 0.0)
            {
                for (const auto& obj : objects)
                {
                    // Flux → electron count over the full integration.
                    const f64 flux_jy = instruments::SNRCalculator::magnitude_to_flux_jy(
                        static_cast<f64>(obj.mag_v), band.center_wavelength_nm);
                    const f64 electron_rate = compute_electron_rate(
                        flux_jy,
                        band.bandwidth_nm,
                        band.center_wavelength_nm,
                        collecting_area_m2,
                        efficiency);
                    const f64 total_electrons = electron_rate * params.integration_time_s;
                    if (total_electrons <= 0.0) { continue; }

                    // Project (RA, Dec) → detector pixel.
                    const auto [x_px, y_px] = project_gnomonic(
                        obj.ra, obj.dec,
                        params.ra_rad, params.dec_rad,
                        pixel_scale, params.width_px, params.height_px);

                    // Skip objects far outside the image (> PSF radius + margin).
                    constexpr f64 kEdgeMarginPx = 50.0;
                    if (x_px < -kEdgeMarginPx ||
                        x_px > static_cast<f64>(params.width_px)  + kEdgeMarginPx ||
                        y_px < -kEdgeMarginPx ||
                        y_px > static_cast<f64>(params.height_px) + kEdgeMarginPx)
                    {
                        continue;
                    }

                    // Accumulate PSF-spread flux into the image.
                    Psf::render_point_source(img, x_px, y_px, total_electrons, psf_fwhm_px);
                }
            }

            // -- Sky background + noise (per pixel) ---------------------------
            const f64 bg_electrons = params.sky_background_e_per_s_px
                                     * params.integration_time_s;

            for (u32 py = 0; py < params.height_px; ++py)
            {
                for (u32 px = 0; px < params.width_px; ++px)
                {
                    // Total expected count (signal + background), clamped ≥ 0.
                    const f64 signal     = static_cast<f64>(img(px, py));
                    const f64 mean_count = std::max(signal + bg_electrons, 0.0);

                    // Poisson noise on (signal + background).
                    std::poisson_distribution<long long> poisson_dist(mean_count);
                    const f64 poisson_sample = static_cast<f64>(poisson_dist(rng));

                    // Gaussian read noise.
                    std::normal_distribution<f64> gauss_dist(0.0, params.read_noise_electrons);
                    const f64 read_noise = gauss_dist(rng);

                    img(px, py) = static_cast<f32>(poisson_sample + read_noise);
                }
            }
        }

        return result;
    }

} // namespace parallax::imaging

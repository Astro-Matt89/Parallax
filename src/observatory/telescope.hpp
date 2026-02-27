#pragma once
// observatory/telescope.hpp - Telescope and detector simulation
//
// Models physical optics: aperture, focal length, detector properties,
// limiting magnitude, and point spread function (PSF) parameters.

#include <string>
#include <cmath>
#include <algorithm>
#include <numbers>

namespace parallax {

// -----------------------------------------------------------------------
// Camera / detector description
// -----------------------------------------------------------------------
struct Detector {
    std::string name{"Generic CCD"};
    int    pixel_width{2048};          ///< Sensor width [pixels]
    int    pixel_height{2048};         ///< Sensor height [pixels]
    double pixel_size_um{9.0};         ///< Physical pixel pitch [μm]
    double read_noise_e{5.0};          ///< Read noise [e- RMS]
    double dark_current_e_s{0.002};    ///< Dark current [e-/s/pixel]
    double quantum_efficiency{0.85};   ///< Peak QE [0..1]
    int    bit_depth{16};              ///< ADC bit depth
    double gain{1.0};                  ///< [e-/ADU]
    bool   is_cooled{true};            ///< Thermoelectric cooling active?
};

// -----------------------------------------------------------------------
// Telescope
// -----------------------------------------------------------------------
class Telescope {
public:
    std::string name{"Generic Refractor"};

    double aperture_mm{100.0};         ///< Clear aperture diameter [mm]
    double focal_length_mm{1000.0};    ///< Effective focal length [mm]
    double central_obstruction{0.0};   ///< Fractional obstruction (0..1)
    double reflectivity{1.0};          ///< Combined mirror/lens throughput

    Detector detector;

    // -----------------------------------------------------------------------
    // Derived optical properties
    // -----------------------------------------------------------------------

    /// F-ratio
    double fRatio() const { return focal_length_mm / aperture_mm; }

    /// Pixel scale [arcsec/pixel]
    double pixelScale() const {
        return (detector.pixel_size_um / focal_length_mm) * 206.265;
    }

    /// Field of view [degrees x degrees]
    std::pair<double,double> fieldOfView() const {
        double w = pixelScale() * detector.pixel_width  / 3600.0;
        double h = pixelScale() * detector.pixel_height / 3600.0;
        return {w, h};
    }

    /// Diffraction limit (Rayleigh criterion) [arcsec]
    double diffractionLimit_arcsec(double wavelength_nm = 550.0) const {
        // θ = 1.22 λ/D  (radians) → arcsec
        return 1.22 * (wavelength_nm * 1e-9) / (aperture_mm * 1e-3)
               * (180.0 / std::numbers::pi) * 3600.0;
    }

    /// Collecting area accounting for central obstruction [cm²]
    double collectingArea_cm2() const {
        double D_cm = aperture_mm / 10.0;
        double d_cm = D_cm * central_obstruction;
        return std::numbers::pi / 4.0 * (D_cm*D_cm - d_cm*d_cm);
    }

    /// Photon flux from a V-magnitude source at the detector [photons/s]
    /// Uses a simplified zero-point: V=0 -> 3.63e10 photons/s/m² in V-band.
    double photonFlux(double v_magnitude, double exposure_s = 1.0) const {
        constexpr double kVzeroFlux = 3.63e10; // photons/s/m² for V=0
        double area_m2 = collectingArea_cm2() * 1e-4;
        double flux = kVzeroFlux * std::pow(10.0, -0.4 * v_magnitude);
        return flux * area_m2 * detector.quantum_efficiency
               * reflectivity * exposure_s;
    }

    /// Signal-to-noise ratio for a point source.
    /// @param v_magnitude  Source apparent magnitude
    /// @param exposure_s   Exposure time [seconds]
    /// @param sky_bg_mag   Sky background [mag/arcsec²]
    /// @param seeing_arcsec Atmospheric seeing FWHM [arcsec]
    double snr(double v_magnitude, double exposure_s,
               double sky_bg_mag = 21.0,
               double seeing_arcsec = 2.0) const {
        double signal = photonFlux(v_magnitude, exposure_s);

        // Sky background photons per PSF area
        double psf_fwhm = std::max(seeing_arcsec, diffractionLimit_arcsec());
        double psf_area_arcsec2 = std::numbers::pi / (4.0 * std::log(2.0))
                                * psf_fwhm * psf_fwhm;
        double psf_pixels = psf_area_arcsec2 / (pixelScale() * pixelScale());
        psf_pixels = std::max(psf_pixels, 1.0);

        double sky_flux_per_arcsec2 = photonFlux(sky_bg_mag, exposure_s);
        double sky_noise = std::sqrt(sky_flux_per_arcsec2 * psf_area_arcsec2);

        // Read noise and dark current
        double read_n  = detector.read_noise_e * std::sqrt(psf_pixels);
        double dark_n  = std::sqrt(detector.dark_current_e_s * exposure_s * psf_pixels);

        double noise = std::sqrt(signal + sky_noise*sky_noise
                                 + read_n*read_n + dark_n*dark_n);
        return (noise > 0.0) ? signal / noise : 0.0;
    }

    /// Limiting magnitude achievable at SNR=5 for a given exposure.
    double limitingMagnitude(double exposure_s,
                              double sky_bg_mag = 21.0,
                              double seeing_arcsec = 2.0) const {
        // Binary search between mag 1 and 30
        double lo = 1.0, hi = 30.0;
        for (int i = 0; i < 64; ++i) {
            double mid = (lo + hi) * 0.5;
            if (snr(mid, exposure_s, sky_bg_mag, seeing_arcsec) >= 5.0)
                lo = mid;
            else
                hi = mid;
        }
        return (lo + hi) * 0.5;
    }
};

// -----------------------------------------------------------------------
// Factory helpers
// -----------------------------------------------------------------------

/// Create a typical amateur 8" Schmidt-Cassegrain telescope.
inline Telescope makeSchCas8inch() {
    Telescope t;
    t.name               = "8\" Schmidt-Cassegrain";
    t.aperture_mm        = 203.2;
    t.focal_length_mm    = 2032.0;
    t.central_obstruction= 0.34;
    t.reflectivity       = 0.88;
    t.detector.name      = "Monochrome CMOS";
    t.detector.pixel_width  = 3096;
    t.detector.pixel_height = 2080;
    t.detector.pixel_size_um= 6.45;
    t.detector.read_noise_e = 3.5;
    t.detector.dark_current_e_s = 0.001;
    t.detector.quantum_efficiency = 0.90;
    t.detector.bit_depth = 12;
    t.detector.gain      = 0.5;
    return t;
}

/// Create a large professional 1-metre reflector.
inline Telescope make1mReflector() {
    Telescope t;
    t.name               = "1-metre Research Reflector";
    t.aperture_mm        = 1000.0;
    t.focal_length_mm    = 8000.0;
    t.central_obstruction= 0.20;
    t.reflectivity       = 0.85;
    t.detector.name      = "Cooled Scientific CCD";
    t.detector.pixel_width  = 4096;
    t.detector.pixel_height = 4096;
    t.detector.pixel_size_um= 13.5;
    t.detector.read_noise_e = 4.0;
    t.detector.dark_current_e_s = 0.0005;
    t.detector.quantum_efficiency = 0.95;
    t.detector.bit_depth = 16;
    t.detector.gain      = 1.1;
    return t;
}

} // namespace parallax

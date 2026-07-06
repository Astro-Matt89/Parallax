#pragma once

/// @file array_instrument.hpp
/// @brief ArrayInstrument — the player's multi-station, multispectral array.
///
/// Sprint 10a operates the array in TOTAL-POWER mode: active stations sum their
/// collecting areas into one effective aperture for SNR, while angular resolution
/// is limited by the diffraction limit of the single largest aperture. Baselines
/// / aperture synthesis (microarcsecond resolution) arrive in Sprint 10b.

#include "instruments/spectral_band.hpp"
#include "instruments/station.hpp"

#include "core/types.hpp"

#include <span>
#include <string>
#include <vector>

namespace parallax::instruments
{
    /// @brief A configurable array of collecting stations observing in one or
    ///        more spectral bands.
    class ArrayInstrument
    {
    public:
        ArrayInstrument(u64 id, std::string name);

        /// @brief Build the default "Glasswing Array" (4 stations, 2 active bands).
        [[nodiscard]] static ArrayInstrument create_default();

        // -- Station management ---------------------------------------------
        void add_station(const Station& station);
        void set_station_active(u32 index, bool active);
        [[nodiscard]] std::span<const Station> get_stations() const;
        [[nodiscard]] u32 get_active_station_count() const;

        /// @brief Combined collecting area of active stations (m², total-power mode).
        [[nodiscard]] f64 get_total_collecting_area_m2() const;

        /// @brief Angular resolution (arcsec) at a wavelength.
        ///
        /// Sprint 10a: diffraction limit of the largest active single aperture
        /// (theta = 1.22 * lambda / D). Sprint 10b overrides this with a
        /// baseline-based resolution.
        [[nodiscard]] f64 get_angular_resolution_arcsec(f64 wavelength_nm) const;

        // -- Spectral bands --------------------------------------------------
        [[nodiscard]] std::span<const SpectralBand> get_bands() const;
        void set_band_active(u32 index, bool active);

        /// @brief Indices of bands that are both unlocked and active.
        [[nodiscard]] std::vector<u32> get_active_bands() const;

        /// @brief Field of view (arcsec) — simplified detector/focal config.
        [[nodiscard]] f64 get_fov_arcsec() const;

        [[nodiscard]] u64 get_id() const;
        [[nodiscard]] const std::string& get_name() const;

    private:
        u64 m_id;
        std::string m_name;
        std::vector<Station> m_stations;
        std::vector<SpectralBand> m_bands;
        f64 m_fov_arcsec = 60.0;  ///< Default 1 arcmin FOV.
    };
}

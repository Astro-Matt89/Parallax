/// @file array_instrument.cpp
/// @brief ArrayInstrument implementation (total-power mode, Sprint 10a).

#include "instruments/array_instrument.hpp"

#include "astro/observer.hpp"
#include "core/types.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace parallax::instruments
{
    namespace
    {
        using astro::ParentBody;
        using astro_constants::kPi;
        using astro_constants::kRadToDeg;

        /// Diffraction-limit constant for a circular aperture (theta = 1.22 * lambda / D).
        constexpr f64 kRayleighFactor = 1.22;

        /// Arcseconds per full circle (360 * 3600).
        constexpr f64 kArcsecPerDegree = 3600.0;

        /// Convert an angle in radians to arcseconds.
        [[nodiscard]] f64 rad_to_arcsec(f64 radians) noexcept
        {
            return radians * kRadToDeg * kArcsecPerDegree;
        }

        /// Circular collecting area for a given aperture diameter (metres -> m^2).
        [[nodiscard]] f64 circular_area_m2(f64 diameter_m) noexcept
        {
            const f64 radius = diameter_m * 0.5;
            return kPi * radius * radius;
        }
    }

    ArrayInstrument::ArrayInstrument(u64 id, std::string name)
        : m_id {id}
        , m_name {std::move(name)}
    {
    }

    void ArrayInstrument::add_station(const Station& station)
    {
        m_stations.push_back(station);
    }

    void ArrayInstrument::set_station_active(u32 index, bool active)
    {
        if (index < m_stations.size())
        {
            m_stations[index].is_active = active;
        }
    }

    std::span<const Station> ArrayInstrument::get_stations() const
    {
        return std::span<const Station>(m_stations);
    }

    u32 ArrayInstrument::get_active_station_count() const
    {
        return static_cast<u32>(std::count_if(
            m_stations.begin(),
            m_stations.end(),
            [](const Station& station) { return station.is_active; }));
    }

    f64 ArrayInstrument::get_total_collecting_area_m2() const
    {
        f64 total_area = 0.0;
        for (const Station& station : m_stations)
        {
            if (station.is_active)
            {
                total_area += circular_area_m2(static_cast<f64>(station.aperture_diameter_m));
            }
        }
        return total_area;
    }

    f64 ArrayInstrument::get_largest_aperture_diameter_m() const
    {
        f64 largest = 0.0;
        for (const Station& station : m_stations)
        {
            if (station.is_active)
            {
                largest = std::max(largest, static_cast<f64>(station.aperture_diameter_m));
            }
        }
        return largest;
    }

    f64 ArrayInstrument::get_angular_resolution_arcsec(f64 wavelength_nm) const
    {
        // Sprint 10a total-power mode: resolution is set by the largest single
        // active aperture (diffraction limit). Baselines are ignored until 10b.
        f64 largest_diameter_m = 0.0;
        for (const Station& station : m_stations)
        {
            if (station.is_active)
            {
                largest_diameter_m =
                    std::max(largest_diameter_m, static_cast<f64>(station.aperture_diameter_m));
            }
        }

        if (largest_diameter_m <= 0.0 || wavelength_nm <= 0.0)
        {
            return 0.0;
        }

        const f64 wavelength_m = wavelength_nm * 1.0e-9;
        const f64 theta_rad = kRayleighFactor * wavelength_m / largest_diameter_m;
        return rad_to_arcsec(theta_rad);
    }

    std::span<const SpectralBand> ArrayInstrument::get_bands() const
    {
        return std::span<const SpectralBand>(m_bands);
    }

    void ArrayInstrument::set_band_active(u32 index, bool active)
    {
        if (index < m_bands.size())
        {
            // Locked bands cannot be activated (progression gate).
            m_bands[index].is_active = active && m_bands[index].is_unlocked;
        }
    }

    std::vector<u32> ArrayInstrument::get_active_bands() const
    {
        std::vector<u32> active_bands;
        for (u32 i = 0; i < m_bands.size(); ++i)
        {
            if (m_bands[i].is_unlocked && m_bands[i].is_active)
            {
                active_bands.push_back(i);
            }
        }
        return active_bands;
    }

    f64 ArrayInstrument::get_fov_arcsec() const
    {
        return m_fov_arcsec;
    }

    u64 ArrayInstrument::get_id() const
    {
        return m_id;
    }

    const std::string& ArrayInstrument::get_name() const
    {
        return m_name;
    }

    ArrayInstrument ArrayInstrument::create_default()
    {
        // "Glasswing Array" — EHT-inspired multispectral array (see sprint_10a.md).
        ArrayInstrument array {0x0A01, "Glasswing Array"};

        // Tycho Primary — Moon, 12 m, vacuum. Coordinates from CLAUDE.md Section 9.
        array.add_station(Station{
            .name = "Tycho Primary",
            .body = astro::ParentBody::Moon,
            .latitude_rad = -43.31 * astro_constants::kDegToRad,
            .longitude_rad = -11.36 * astro_constants::kDegToRad,
            .elevation_m = -1200.0,
            .aperture_diameter_m = 12.0f,
            .efficiency = 0.85f,
            .has_atmosphere = false,
            .is_active = true,
        });

        // La Palma (Roque de los Muchachos) — Earth, 8 m.
        array.add_station(Station{
            .name = "La Palma",
            .body = astro::ParentBody::Earth,
            .latitude_rad = 28.7569 * astro_constants::kDegToRad,
            .longitude_rad = -17.8925 * astro_constants::kDegToRad,
            .elevation_m = 2396.0,
            .aperture_diameter_m = 8.0f,
            .efficiency = 0.75f,
            .has_atmosphere = true,
            .is_active = true,
        });

        // Mauna Kea — Earth, 10 m.
        array.add_station(Station{
            .name = "Mauna Kea",
            .body = astro::ParentBody::Earth,
            .latitude_rad = 19.8206 * astro_constants::kDegToRad,
            .longitude_rad = -155.4681 * astro_constants::kDegToRad,
            .elevation_m = 4207.0,
            .aperture_diameter_m = 10.0f,
            .efficiency = 0.75f,
            .has_atmosphere = true,
            .is_active = true,
        });

        // Paranal — Earth, 8 m.
        array.add_station(Station{
            .name = "Paranal",
            .body = astro::ParentBody::Earth,
            .latitude_rad = -24.6275 * astro_constants::kDegToRad,
            .longitude_rad = -70.4044 * astro_constants::kDegToRad,
            .elevation_m = 2635.0,
            .aperture_diameter_m = 8.0f,
            .efficiency = 0.75f,
            .has_atmosphere = true,
            .is_active = true,
        });

        // Bands unlocked at game start.
        array.m_bands.push_back(SpectralBand{
            .name = "Visible",
            .center_wavelength_nm = 550.0,
            .bandwidth_nm = 200.0,
            .is_unlocked = true,
            .is_active = true,
        });
        array.m_bands.push_back(SpectralBand{
            .name = "Near-IR",
            .center_wavelength_nm = 1600.0,
            .bandwidth_nm = 400.0,
            .is_unlocked = true,
            .is_active = true,
        });

        // Locked bands (future progression). Wavelengths converted to nm.
        array.m_bands.push_back(SpectralBand{
            .name = "Mid-IR",
            .center_wavelength_nm = 10000.0,
            .bandwidth_nm = 2000.0,
            .is_unlocked = false,
            .is_active = false,
        });
        array.m_bands.push_back(SpectralBand{
            .name = "Radio-K",  // The EHT band (1.3 mm).
            .center_wavelength_nm = 1.3e6,
            .bandwidth_nm = 1.0e5,
            .is_unlocked = false,
            .is_active = false,
        });
        array.m_bands.push_back(SpectralBand{
            .name = "Submm",  // 850 um.
            .center_wavelength_nm = 8.5e5,
            .bandwidth_nm = 5.0e4,
            .is_unlocked = false,
            .is_active = false,
        });

        return array;
    }
}

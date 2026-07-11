/// @file observation_session.cpp
/// @brief ObservationSession implementation.

#include "observation/observation_session.hpp"

#include "imaging/image_formation.hpp"
#include "instruments/array_instrument.hpp"
#include "instruments/snr_calculator.hpp"
#include "universe/universe.hpp"

#include <algorithm>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace parallax::observation
{

namespace
{
    /// Speed of light in vacuum (m/s), used for bandwidth conversion Δν ≈ c·Δλ/λ².
    constexpr f64 kSpeedOfLightMPerS = 299792458.0;

    /// Base receiver/system temperature for vacuum stations (Moon) in Kelvin.
    /// Chosen to represent low-noise cryogenic instrument electronics.
    constexpr f64 kBaseSystemTemperatureK = 35.0;

    /// Atmospheric system-temperature penalty applied to Earth stations (Kelvin).
    /// Encodes additional sky/atmospheric noise versus vacuum operation.
    constexpr f64 kAtmosphereTemperaturePenaltyK = 75.0;

    /// Baseline sky-background term used until Universe exposes a direct background query.
    constexpr f64 kBaseSkyBackground = 1000.0;

    /// Fallback target magnitude when a target lookup is unavailable.
    constexpr f64 kDefaultTargetMagnitude = 20.0;

    /// Default image dimensions for session preview/completion products.
    constexpr u32 kImageWidthPx = 256u;
    constexpr u32 kImageHeightPx = 256u;

    /// Default imaging noise parameters used by session-driven image formation.
    constexpr f64 kImageSkyBackgroundElectronsPerSecondPx = 10.0;
    constexpr f64 kImageReadNoiseElectrons = 3.0;
    constexpr f64 kImageMagLimit = 22.0;

    struct ActiveBand
    {
        u32 index = 0u;
        f64 center_wavelength_nm = 0.0;
        f64 bandwidth_nm = 0.0;
        std::string name;
    };

    struct StationStats
    {
        f64 efficiency = 0.0;
        f64 system_temperature_k = kBaseSystemTemperatureK;
    };

    [[nodiscard]] f64 circular_area_m2(f64 diameter_m)
    {
        const f64 radius = diameter_m * 0.5;
        return astro_constants::kPi * radius * radius;
    }

    [[nodiscard]] f64 bandwidth_nm_to_hz(f64 center_wavelength_nm, f64 bandwidth_nm)
    {
        if (center_wavelength_nm <= 0.0 || bandwidth_nm <= 0.0)
        {
            return 0.0;
        }

        const f64 wavelength_m = center_wavelength_nm * 1.0e-9;
        const f64 bandwidth_m = bandwidth_nm * 1.0e-9;
        return kSpeedOfLightMPerS * bandwidth_m / (wavelength_m * wavelength_m);
    }

    [[nodiscard]] std::vector<ActiveBand> get_active_bands(
        const instruments::ArrayInstrument& instrument)
    {
        std::vector<ActiveBand> bands;
        const auto band_indices = instrument.get_active_bands();
        const auto instrument_bands = instrument.get_bands();

        bands.reserve(band_indices.size());
        for (const u32 band_index : band_indices)
        {
            if (band_index >= instrument_bands.size())
            {
                continue;
            }

            const auto& band = instrument_bands[band_index];
            bands.push_back(ActiveBand{
                .index = band_index,
                .center_wavelength_nm = band.center_wavelength_nm,
                .bandwidth_nm = band.bandwidth_nm,
                .name = band.name,
            });
        }

        return bands;
    }

    [[nodiscard]] StationStats compute_station_stats(
        const instruments::ArrayInstrument& instrument)
    {
        f64 weighted_efficiency = 0.0;
        f64 weighted_temperature = 0.0;
        f64 total_area = 0.0;

        for (const auto& station : instrument.get_stations())
        {
            if (!station.is_active)
            {
                continue;
            }

            const f64 area = circular_area_m2(static_cast<f64>(station.aperture_diameter_m));
            const f64 station_temperature = kBaseSystemTemperatureK
                + (station.has_atmosphere ? kAtmosphereTemperaturePenaltyK : 0.0);

            weighted_efficiency += static_cast<f64>(station.efficiency) * area;
            weighted_temperature += station_temperature * area;
            total_area += area;
        }

        if (total_area <= 0.0)
        {
            return StationStats{};
        }

        return StationStats{
            .efficiency = weighted_efficiency / total_area,
            .system_temperature_k = weighted_temperature / total_area,
        };
    }

    [[nodiscard]] instruments::SNRParameters build_snr_parameters(
        const SessionParameters&             params,
        const instruments::ArrayInstrument&  instrument,
        const universe::Universe&            universe,
        const ActiveBand&                    band)
    {
        f64 target_magnitude = kDefaultTargetMagnitude;
        if (params.target_object_id != 0u)
        {
            if (const auto target = universe.query_object(params.target_object_id); target.has_value())
            {
                target_magnitude = static_cast<f64>(target->mag_v);
            }
        }

        const StationStats station_stats = compute_station_stats(instrument);
        const f64 collecting_area_m2 = instrument.get_total_collecting_area_m2();
        const f64 target_flux_jy = instruments::SNRCalculator::magnitude_to_flux_jy(
            target_magnitude,
            band.center_wavelength_nm);

        const f64 normalized_temperature = station_stats.system_temperature_k / kBaseSystemTemperatureK;
        const f64 sky_background = kBaseSkyBackground * std::max(1.0, normalized_temperature);

        return instruments::SNRParameters{
            .target_flux_jy = target_flux_jy,
            .collecting_area_m2 = collecting_area_m2,
            .efficiency = station_stats.efficiency,
            .bandwidth_hz = bandwidth_nm_to_hz(band.center_wavelength_nm, band.bandwidth_nm),
            .integration_time_s = 0.0,
            .system_temperature_k = station_stats.system_temperature_k,
            .sky_background = sky_background,
            .num_stations = std::max(1u, instrument.get_active_station_count()),
        };
    }

    [[nodiscard]] f64 compute_physical_cumulative_snr(
        const SessionParameters&             params,
        const instruments::ArrayInstrument&  instrument,
        const universe::Universe&            universe,
        f64                                  elapsed_seconds)
    {
        if (elapsed_seconds <= 0.0)
        {
            return 0.0;
        }

        const auto active_bands = get_active_bands(instrument);
        if (active_bands.empty())
        {
            return 0.0;
        }

        // Headline session SNR uses the best active band at the current integration.
        f64 best_band_snr = 0.0;
        for (const ActiveBand& band : active_bands)
        {
            const auto snr_params = build_snr_parameters(params, instrument, universe, band);
            const f64 band_snr = instruments::SNRCalculator::compute_snr_cumulative(
                snr_params,
                elapsed_seconds);
            best_band_snr = std::max(best_band_snr, band_snr);
        }

        return best_band_snr;
    }

    [[nodiscard]] std::vector<imaging::BandSpec> make_image_band_specs(
        const instruments::ArrayInstrument& instrument)
    {
        std::vector<imaging::BandSpec> band_specs;
        for (const auto& band : get_active_bands(instrument))
        {
            band_specs.push_back(imaging::BandSpec{
                .band_name = band.name,
                .band_index = band.index,
                .center_wavelength_nm = band.center_wavelength_nm,
                .bandwidth_nm = band.bandwidth_nm,
            });
        }
        return band_specs;
    }
} // namespace

// =============================================================================
// Constructor
// =============================================================================

ObservationSession::ObservationSession(std::uint64_t id, SessionParameters params)
    : m_id     {id}
    , m_params {std::move(params)}
    , m_progress {}
{
}

// =============================================================================
// tick
// =============================================================================

void ObservationSession::tick(double                          current_jd,
                              double                          dt_seconds,
                              const universe::Universe&       universe,
                              const instruments::ArrayInstrument& instrument)
{
    // ----- Scheduled → InProgress transition --------------------------------
    if (m_progress.state == SessionState::Scheduled)
    {
        if (current_jd >= m_params.start_julian_date)
        {
            m_progress.state = SessionState::InProgress;
            m_progress.log.push_back(
                std::format("Session {} started at JD {:.6f}", m_id, current_jd));
        }
        return;
    }

    // ----- InProgress: accumulate SNR and advance time ----------------------
    if (m_progress.state == SessionState::InProgress)
    {
        const double dt_hours = dt_seconds / 3600.0;

        m_progress.elapsed_hours   += dt_hours;
        m_progress.accumulated_snr = compute_physical_cumulative_snr(
            m_params,
            instrument,
            universe,
            m_progress.elapsed_hours * 3600.0);

        // Guard against zero or negative planned duration.
        const double planned = m_params.planned_duration_hours;
        if (planned > 0.0)
        {
            m_progress.completion_fraction =
                std::clamp(m_progress.elapsed_hours / planned, 0.0, 1.0);
        }
        else
        {
            m_progress.completion_fraction = 1.0;
        }

        // ----- InProgress → Completed transition ----------------------------
        if (m_progress.elapsed_hours >= planned)
        {
            m_progress.state = SessionState::Completed;
            m_progress.log.push_back(
                std::format("Session {} completed after {:.4f} h (physical SNR {:.2f})",
                            m_id,
                            m_progress.elapsed_hours,
                            m_progress.accumulated_snr));
        }

        return;
    }

    // Any other state (Completed, Aborted, Failed): no-op.
}

imaging::MultispectralImage ObservationSession::form_image(
    const instruments::ArrayInstrument& instrument,
    const universe::Universe&           universe,
    const imaging::IObjectSource&       object_source,
    std::uint64_t                       seed) const
{
    f64 target_ra_rad = m_params.target_region.center_ra;
    f64 target_dec_rad = m_params.target_region.center_dec;

    if (m_params.target_object_id != 0u)
    {
        if (const auto target = universe.query_object(m_params.target_object_id); target.has_value())
        {
            target_ra_rad = target->ra;
            target_dec_rad = target->dec;
        }
    }

    auto bands = make_image_band_specs(instrument);

    imaging::ImageFormationParams params{
        .ra_rad = target_ra_rad,
        .dec_rad = target_dec_rad,
        .fov_arcsec = instrument.get_fov_arcsec(),
        .width_px = kImageWidthPx,
        .height_px = kImageHeightPx,
        .bands = std::move(bands),
        .integration_time_s = std::max(0.0, m_progress.elapsed_hours * 3600.0),
        .sky_background_e_per_s_px = kImageSkyBackgroundElectronsPerSecondPx,
        .read_noise_electrons = kImageReadNoiseElectrons,
        .mag_limit = static_cast<float>(kImageMagLimit),
        .seed = seed,
    };

    if (params.bands.empty())
    {
        return imaging::MultispectralImage{
            params.width_px,
            params.height_px,
            params.fov_arcsec / static_cast<f64>(params.width_px)};
    }

    return imaging::ImageFormation::form(params, instrument, object_source);
}

// =============================================================================
// produce_data
// =============================================================================

DataRecord ObservationSession::produce_data() const
{
    DataRecord rec;
    rec.id               = 0;   // Reserved; the Knowledge System or caller assigns
                                // a unique record ID when ingesting the record.
    rec.session_id       = m_id;
    rec.target_object_id = m_params.target_object_id;
    rec.type             = DataType::Mock;
    rec.technique        = m_params.technique;
    rec.observation_jd   = m_params.start_julian_date;
    rec.duration_hours   = m_progress.elapsed_hours;
    rec.achieved_snr     = m_progress.accumulated_snr;
    // measurements and uncertainties are intentionally empty:
    // analysis will populate them in a later task.
    return rec;
}

// =============================================================================
// Accessors
// =============================================================================

std::uint64_t ObservationSession::id() const noexcept
{
    return m_id;
}

const SessionParameters& ObservationSession::parameters() const noexcept
{
    return m_params;
}

const SessionProgress& ObservationSession::progress() const noexcept
{
    return m_progress;
}

void ObservationSession::set_state(SessionState state)
{
    m_progress.state = state;
}

} // namespace parallax::observation

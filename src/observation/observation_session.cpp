/// @file observation_session.cpp
/// @brief ObservationSession implementation.

#include "observation/observation_session.hpp"

#include "universe/universe.hpp"

#include <algorithm>
#include <format>

namespace parallax::observation
{

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
                               [[maybe_unused]]
                               const universe::Universe&       universe,
                               [[maybe_unused]]
                               IInstrument*                    instrument)
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

        // MOCK placeholder. Sprint 09+ will replace with a real SNR model
        // (instrument aperture, target magnitude, sky brightness, seeing,
        // airmass, etc.).
        const double snr_gain = 5.0 * dt_hours;

        m_progress.accumulated_snr += snr_gain;
        m_progress.elapsed_hours   += dt_hours;

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
                std::format("Session {} completed after {:.4f} h (SNR {:.2f})",
                            m_id,
                            m_progress.elapsed_hours,
                            m_progress.accumulated_snr));
        }

        return;
    }

    // Any other state (Completed, Aborted, Failed): no-op.
}

// =============================================================================
// produce_data
// =============================================================================

DataRecord ObservationSession::produce_data() const
{
    DataRecord rec;
    rec.id               = 0;   // Caller / scheduler assigns on harvest.
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

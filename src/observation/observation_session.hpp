#pragma once

/// @file observation_session.hpp
/// @brief ObservationSession — lifecycle and SNR-accumulation for a single planned observation.

#include "observation/data_record.hpp"
#include "observation/session_types.hpp"

#include <cstdint>

namespace parallax::universe
{
class Universe;
} // namespace parallax::universe

// IInstrument is defined in Task 8.6+.  Forward-declare only.
class IInstrument;

namespace parallax::observation
{

/// @brief One scheduled (or active, or completed) observation session.
///
/// A session is created by `SessionScheduler::schedule()` and advanced by
/// `SessionScheduler::update()`, which calls `tick()` once per simulation
/// frame.  When the session reaches `SessionState::Completed` the caller
/// may call `produce_data()` to extract the resulting `DataRecord`, then
/// use `SessionScheduler::harvest()` to remove the session from the map.
class ObservationSession
{
public:
    /// @param id     Unique session identifier assigned by the scheduler.
    /// @param params Immutable parameters that describe the planned observation.
    ObservationSession(std::uint64_t id, SessionParameters params);

    /// @brief Advance the session by one simulation step.
    ///
    /// @param current_jd  Current simulation time as a Julian Date.
    ///                    Passed explicitly because Universe has no JD accessor.
    ///                    TODO: once Universe exposes current_julian_date(),
    ///                    this parameter can be removed.
    /// @param dt_seconds  Wall-clock equivalent time elapsed this frame (seconds).
    /// @param universe    Read-only access to the universe (reserved for future
    ///                    horizon/atmosphere checks in Sprint 09+).
    /// @param instrument  Active instrument for this session, or nullptr when none
    ///                    is yet wired up.  Unused until Task 8.6+.
    void tick(double                          current_jd,
              double                          dt_seconds,
              const universe::Universe&       universe,
              IInstrument*                    instrument);

    /// @brief Build and return the DataRecord produced by this session.
    ///
    /// `measurements` and `uncertainties` are intentionally left empty — analysis
    /// is added in a later task.  Call only after state == Completed.
    [[nodiscard]] DataRecord produce_data() const;

    // -------------------------------------------------------------------------
    // Accessors
    // -------------------------------------------------------------------------

    [[nodiscard]] std::uint64_t            id()         const noexcept;
    [[nodiscard]] const SessionParameters& parameters() const noexcept;
    [[nodiscard]] const SessionProgress&   progress()   const noexcept;

    /// @brief Override the session state (used by the scheduler to abort).
    void set_state(SessionState state);

private:
    std::uint64_t     m_id;
    SessionParameters m_params;
    SessionProgress   m_progress;
};

} // namespace parallax::observation

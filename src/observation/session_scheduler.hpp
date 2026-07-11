#pragma once

/// @file session_scheduler.hpp
/// @brief SessionScheduler — owns and drives all ObservationSession objects.

#include "observation/observation_session.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace parallax::universe
{
class Universe;
} // namespace parallax::universe

namespace parallax::instruments
{
class ArrayInstrument;
} // namespace parallax::instruments

namespace parallax::observation
{

/// @brief Lifecycle manager for all observation sessions.
///
/// The owning system (Application, Task 8.11) should call `update()` once per
/// simulation frame and periodically call `harvest()` on completed sessions to
/// extract their `DataRecord`s and remove them from the scheduler.
class SessionScheduler
{
public:
    SessionScheduler() = default;

    // Non-copyable; movable.
    SessionScheduler(const SessionScheduler&)            = delete;
    SessionScheduler& operator=(const SessionScheduler&) = delete;
    SessionScheduler(SessionScheduler&&)                 = default;
    SessionScheduler& operator=(SessionScheduler&&)      = default;

    // -------------------------------------------------------------------------
    // Scheduling
    // -------------------------------------------------------------------------

    /// @brief Create a new session and add it to the scheduler.
    ///
    /// The session begins in `SessionState::Scheduled` and will transition to
    /// `InProgress` automatically during `update()` when the simulation time
    /// reaches `params.start_julian_date`.
    ///
    /// @return The unique ID assigned to the new session.
    [[nodiscard]] std::uint64_t schedule(const SessionParameters& params);

    /// @brief Request cancellation of a session.
    ///
    /// Transitions the session to `SessionState::Aborted`.  The session
    /// remains in the map so callers can inspect it; it will never be
    /// harvested automatically.
    void abort(std::uint64_t id);

    // -------------------------------------------------------------------------
    // Per-frame update
    // -------------------------------------------------------------------------

    /// @brief Advance all active sessions by one simulation step.
    ///
    /// @param current_jd  Current simulation time as a Julian Date.
    /// @param dt_seconds  Elapsed time this frame (seconds).
    /// @param universe    Read-only universe reference forwarded to each session.
    /// @param instrument  Active array instrument forwarded to each session.
    void update(double                    current_jd,
                double                    dt_seconds,
                const universe::Universe& universe,
                const instruments::ArrayInstrument& instrument);

    // -------------------------------------------------------------------------
    // Queries (non-owning views)
    // -------------------------------------------------------------------------

    /// @brief Sessions with state `Scheduled` or `InProgress`.
    [[nodiscard]] std::vector<const ObservationSession*> get_active()    const;

    /// @brief Sessions with state `Completed` that have not yet been harvested.
    [[nodiscard]] std::vector<const ObservationSession*> get_completed() const;

    // -------------------------------------------------------------------------
    // Harvest
    // -------------------------------------------------------------------------

    /// @brief Consume a completed session's DataRecord and remove the session.
    ///
    /// @return The produced DataRecord, or `std::nullopt` if the session does
    ///         not exist or its state is not `Completed`.
    [[nodiscard]] std::optional<DataRecord> harvest(std::uint64_t id);

private:
    std::unordered_map<std::uint64_t, std::unique_ptr<ObservationSession>> m_sessions;

    /// Next ID to assign.  0 is reserved for "none" / historical records.
    std::uint64_t m_next_id {1};
};

} // namespace parallax::observation

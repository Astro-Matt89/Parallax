/// @file session_scheduler.cpp
/// @brief SessionScheduler implementation.

#include "observation/session_scheduler.hpp"

#include "universe/universe.hpp"

namespace parallax::observation
{

// =============================================================================
// schedule
// =============================================================================

std::uint64_t SessionScheduler::schedule(const SessionParameters& params)
{
    const std::uint64_t id = m_next_id++;
    m_sessions.emplace(id, std::make_unique<ObservationSession>(id, params));
    return id;
}

// =============================================================================
// abort
// =============================================================================

void SessionScheduler::abort(std::uint64_t id)
{
    auto it = m_sessions.find(id);
    if (it != m_sessions.end())
    {
        it->second->set_state(SessionState::Aborted);
    }
}

// =============================================================================
// update
// =============================================================================

void SessionScheduler::update(double                    current_jd,
                               double                    dt_seconds,
                               const universe::Universe& universe)
{
    // Collect sessions to tick.  We must not mutate m_sessions while iterating.
    // (Sessions only change internal state during tick — no insertions/erasures
    //  happen here — but we use a local list for clarity and future-proofing.)
    std::vector<std::uint64_t> active_ids;
    active_ids.reserve(m_sessions.size());

    for (const auto& [id, session] : m_sessions)
    {
        const SessionState state = session->progress().state;
        if (state == SessionState::Scheduled || state == SessionState::InProgress)
        {
            active_ids.push_back(id);
        }
    }

    for (const std::uint64_t id : active_ids)
    {
        auto it = m_sessions.find(id);
        if (it != m_sessions.end())
        {
            // TODO(Task 8.6+): look up the real instrument by
            // it->second->parameters().instrument_id and pass it here.
            it->second->tick(current_jd, dt_seconds, universe, nullptr);
        }
    }
}

// =============================================================================
// get_active
// =============================================================================

std::vector<const ObservationSession*> SessionScheduler::get_active() const
{
    std::vector<const ObservationSession*> result;
    for (const auto& [id, session] : m_sessions)
    {
        const SessionState state = session->progress().state;
        if (state == SessionState::Scheduled || state == SessionState::InProgress)
        {
            result.push_back(session.get());
        }
    }
    return result;
}

// =============================================================================
// get_completed
// =============================================================================

std::vector<const ObservationSession*> SessionScheduler::get_completed() const
{
    std::vector<const ObservationSession*> result;
    for (const auto& [id, session] : m_sessions)
    {
        if (session->progress().state == SessionState::Completed)
        {
            result.push_back(session.get());
        }
    }
    return result;
}

// =============================================================================
// harvest
// =============================================================================

std::optional<DataRecord> SessionScheduler::harvest(std::uint64_t id)
{
    auto it = m_sessions.find(id);
    if (it == m_sessions.end())
    {
        return std::nullopt;
    }

    if (it->second->progress().state != SessionState::Completed)
    {
        return std::nullopt;
    }

    DataRecord rec = it->second->produce_data();
    m_sessions.erase(it);
    return rec;
}

} // namespace parallax::observation

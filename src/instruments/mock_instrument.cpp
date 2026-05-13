/// @file mock_instrument.cpp
/// @brief MockInstrument implementation.

#include "instruments/mock_instrument.hpp"

#include <utility>

namespace parallax::instruments
{

namespace
{
constexpr float kSnrRatePerHour = 5.0f;
}

MockInstrument::MockInstrument(std::uint64_t id, std::string name)
    : m_id {id}
    , m_name {std::move(name)}
{
}

std::uint64_t MockInstrument::id() const noexcept
{
    return m_id;
}

const std::string& MockInstrument::name() const noexcept
{
    return m_name;
}

float MockInstrument::get_snr_rate_per_hour() const noexcept
{
    // MOCK placeholder. Sprint 09+ replaces this flat value with
    // physics-based SNR derived from instrument + observing conditions.
    return kSnrRatePerHour;
}

} // namespace parallax::instruments

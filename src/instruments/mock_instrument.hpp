#pragma once

/// @file mock_instrument.hpp
/// @brief MockInstrument — Sprint 08 placeholder instrument.

#include <cstdint>
#include <string>

namespace parallax::instruments
{

class MockInstrument
{
public:
    MockInstrument(std::uint64_t id, std::string name = "Magic Instrument");

    [[nodiscard]] std::uint64_t      id() const noexcept;
    [[nodiscard]] const std::string& name() const noexcept;

    // MOCK placeholder. Sprint 09+ will replace this with a proper
    // Instrument base class + TelescopeSystem + Sensor that derives
    // SNR from aperture, target magnitude, sky brightness, seeing, etc.
    [[nodiscard]] float get_snr_rate_per_hour() const noexcept;

private:
    std::uint64_t m_id;
    std::string   m_name;
};

} // namespace parallax::instruments

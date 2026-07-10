# -----------------------------------------------------------------
# Instruments module
# -----------------------------------------------------------------

target_sources(parallax PRIVATE
    mock_instrument.hpp
    mock_instrument.cpp
    station.hpp
    spectral_band.hpp
    array_instrument.hpp
    array_instrument.cpp
    snr_calculator.hpp
    snr_calculator.cpp
)

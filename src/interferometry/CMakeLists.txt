# -----------------------------------------------------------------
# Interferometry module
# -----------------------------------------------------------------

target_sources(parallax PRIVATE
    array_config.hpp
    array_config.cpp
    ephemeris.hpp
    ephemeris.cpp
    mulberry32.hpp
    kolmogorov.hpp
    kolmogorov.cpp
    uv_sampling.hpp
    uv_sampling.cpp
)

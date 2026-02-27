// discovery/discovery_engine.cpp
#include "discovery_engine.hpp"
#include <cmath>

namespace parallax {

bool DiscoveryEngine::recordObservation(Discovery& disc, Observation obs) const {
    obs.is_detection = (obs.snr >= kDetectionSnrThreshold);
    disc.observations.push_back(obs);

    if (obs.snr >= kDiscoverySnrThreshold) {
        disc.n_confirmations++;
        if (disc.n_confirmations >= kRequiredConfirmations) {
            disc.confirmed = true;
        }
        return true;
    }
    return false;
}

} // namespace parallax

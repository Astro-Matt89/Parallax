#pragma once
// observatory/observer.hpp - Observer and observatory site management
//
// Combines geographic location, local sidereal time, and instrument
// selection into a single simulation context.

#include "core/math/coordinates.hpp"
#include "observatory/telescope.hpp"
#include "observatory/atmosphere.hpp"
#include <string>
#include <cmath>

namespace parallax {

// -----------------------------------------------------------------------
// Julian Date utilities
// -----------------------------------------------------------------------

/// Compute Julian Date from calendar date (UTC).
/// Valid for dates from 1800 to 2200.
inline double julianDate(int year, int month, int day,
                          double hour_ut = 0.0) {
    int a = (14 - month) / 12;
    int y = year + 4800 - a;
    int m = month + 12*a - 3;
    double jdn = day + (153*m + 2)/5 + 365*y + y/4 - y/100 + y/400 - 32045;
    return jdn - 0.5 + hour_ut / 24.0;
}

/// Greenwich Mean Sidereal Time [degrees] from Julian Date.
inline double gmst_deg(double jd) {
    double T = (jd - 2451545.0) / 36525.0;
    double gmst = 280.46061837 + 360.98564736629 * (jd - 2451545.0)
                + T*T*0.000387933 - T*T*T/38710000.0;
    return std::fmod(gmst, 360.0);
}

/// Local Sidereal Time [degrees] for a given JD and longitude.
inline double lst_deg(double jd, double lon_deg) {
    return std::fmod(gmst_deg(jd) + lon_deg + 360.0, 360.0);
}

// -----------------------------------------------------------------------
// ObservingSite
// -----------------------------------------------------------------------
struct ObservingSite {
    std::string        name{"Default Observatory"};
    GeographicLocation location;
    AtmosphericConditions conditions;
    int                timezone_offset_h{0}; ///< UTC offset [hours]
};

// -----------------------------------------------------------------------
// ObservingSession
// -----------------------------------------------------------------------
/// An active observing session combining site, instrument, and time.
class ObservingSession {
public:
    ObservingSession(const ObservingSite& site, const Telescope& scope,
                     double jd_start)
        : m_site(site), m_telescope(scope),
          m_atmosphere(site.conditions), m_jd(jd_start) {}

    // Accessors
    const ObservingSite&    site()       const { return m_site; }
    const Telescope&        telescope()  const { return m_telescope; }
    const AtmosphericModel& atmosphere() const { return m_atmosphere; }
    double                  jd()         const { return m_jd; }

    /// Advance simulated time.
    void advanceTime(double hours) { m_jd += hours / 24.0; }

    /// Current Local Sidereal Time [degrees].
    double lst() const {
        return lst_deg(m_jd, m_site.location.lon_deg);
    }

    /// Convert equatorial -> horizontal for the current LST.
    Horizontal toHorizontal(const Equatorial& eq) const {
        double ha = hourAngle(lst(), eq.ra_deg);
        return equatorialToHorizontal(eq, ha, m_site.location.lat_deg);
    }

    /// Is a given equatorial position above the minimum useful altitude?
    bool isVisible(const Equatorial& eq, double min_alt_deg = 15.0) const {
        return toHorizontal(eq).alt_deg >= min_alt_deg;
    }

    /// Effective limiting magnitude accounting for atmosphere and telescope.
    double limitingMagnitude(const Equatorial& eq,
                              double exposure_s = 60.0) const {
        Horizontal hor = toHorizontal(eq);
        double sky_bg = m_atmosphere.skyBackground(hor.alt_deg);
        double seeing = m_atmosphere.effectiveSeeing_arcsec(hor.alt_deg);
        return m_telescope.limitingMagnitude(exposure_s, sky_bg, seeing);
    }

    /// Signal-to-noise ratio for observing a source.
    double snr(const Equatorial& eq, double v_magnitude,
               double exposure_s = 60.0) const {
        Horizontal hor = toHorizontal(eq);
        double app_mag = m_atmosphere.apparentMagnitude(v_magnitude, hor.alt_deg);
        double sky_bg  = m_atmosphere.skyBackground(hor.alt_deg);
        double seeing  = m_atmosphere.effectiveSeeing_arcsec(hor.alt_deg);
        return m_telescope.snr(app_mag, exposure_s, sky_bg, seeing);
    }

private:
    ObservingSite    m_site;
    Telescope        m_telescope;
    AtmosphericModel m_atmosphere;
    double           m_jd{0.0};
};

// -----------------------------------------------------------------------
// Factory helpers
// -----------------------------------------------------------------------

/// Create a typical dark-sky mountain observatory.
inline ObservingSite makeMaunaKeaSite() {
    ObservingSite site;
    site.name = "Mauna Kea Observatory";
    site.location = {19.8207, -155.4681, 4205.0};  // lat, lon, elevation
    site.conditions.seeing_arcsec = 0.5;
    site.conditions.extinction_mag = 0.10;
    site.conditions.bortle = 1;
    site.conditions.transparency = 0.98;
    site.timezone_offset_h = -10;
    return site;
}

/// Create a typical suburban backyard observatory.
inline ObservingSite makeBackyardSite() {
    ObservingSite site;
    site.name = "Backyard Observatory";
    site.location = {51.5, -0.1, 10.0};  // London-like
    site.conditions.seeing_arcsec = 3.0;
    site.conditions.extinction_mag = 0.30;
    site.conditions.bortle = 7;
    site.conditions.transparency = 0.75;
    site.timezone_offset_h = 0;
    return site;
}

} // namespace parallax

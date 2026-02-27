// rendering/renderer.cpp
#include "rendering/renderer.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <algorithm>

namespace parallax {

// -----------------------------------------------------------------------
// Glyph table: . · * ✦ ●  (index 0 = faintest)
// -----------------------------------------------------------------------
char ConsoleRenderer::brightnessGlyph(double brightness) {
    // Map [0,1] to 5 levels
    if (brightness < 0.001) return ' ';
    if (brightness < 0.01)  return '.';
    if (brightness < 0.05)  return '+';
    if (brightness < 0.20)  return '*';
    if (brightness < 0.50)  return 'o';
    return '@';
}

void ConsoleRenderer::hline(std::ostream& out, int w, char c) {
    out << '+';
    for (int i = 0; i < w - 2; ++i) out << c;
    out << '+' << '\n';
}

// -----------------------------------------------------------------------
// renderStarField
// -----------------------------------------------------------------------
void ConsoleRenderer::renderStarField(std::ostream& out,
                                       const std::vector<StarRenderRecord>& stars,
                                       const std::string& title) const {
    // Build a character grid
    std::vector<std::vector<char>> grid(
        static_cast<std::size_t>(viewport_h),
        std::vector<char>(static_cast<std::size_t>(viewport_w), ' '));

    for (const auto& rec : stars) {
        int px = static_cast<int>(rec.screen_x * viewport_w);
        int py = static_cast<int>(rec.screen_y * viewport_h);
        if (px < 0 || px >= viewport_w) continue;
        if (py < 0 || py >= viewport_h) continue;
        char g = brightnessGlyph(rec.brightness);
        // Only overwrite if brighter
        char existing = grid[static_cast<std::size_t>(py)]
                             [static_cast<std::size_t>(px)];
        if (g > existing) {
            grid[static_cast<std::size_t>(py)]
                [static_cast<std::size_t>(px)] = g;
        }
    }

    hline(out, viewport_w, '=');
    if (!title.empty()) {
        out << "| " << std::left << std::setw(viewport_w - 4)
            << title << " |" << '\n';
        hline(out, viewport_w, '-');
    }
    for (int row = 0; row < viewport_h; ++row) {
        out << '|';
        for (int col = 0; col < viewport_w - 2; ++col) {
            out << grid[static_cast<std::size_t>(row)]
                       [static_cast<std::size_t>(col)];
        }
        out << '|' << '\n';
    }
    hline(out, viewport_w, '=');
}

// -----------------------------------------------------------------------
// renderStatusPanel
// -----------------------------------------------------------------------
void ConsoleRenderer::renderStatusPanel(std::ostream& out,
                                         const ObservingSession& session,
                                         const Equatorial& target,
                                         double target_snr) const {
    Horizontal hor = session.toHorizontal(target);
    double X = AtmosphericModel::airmass(hor.alt_deg);
    double seeing = session.atmosphere().effectiveSeeing_arcsec(hor.alt_deg);
    double ext    = session.atmosphere().extinction_mag(hor.alt_deg);

    hline(out, viewport_w, '=');
    out << "| PARALLAX OBSERVATORY CONSOLE"
        << std::setw(viewport_w - 32) << "" << "|\n";
    hline(out, viewport_w, '-');

    auto row = [&](const std::string& lbl, const std::string& val) {
        out << "| " << std::left << std::setw(22) << lbl
            << " : " << std::left << std::setw(viewport_w - 29) << val
            << "|\n";
    };

    auto fmtd = [](double v, int prec = 2) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(prec) << v;
        return ss.str();
    };

    row("Site", session.site().name);
    row("JD", fmtd(session.jd(), 5));
    row("LST", fmtd(session.lst(), 2) + " deg");
    hline(out, viewport_w, '-');
    row("Target RA",  fmtd(target.ra_deg,  4) + " deg");
    row("Target Dec", fmtd(target.dec_deg, 4) + " deg");
    row("Azimuth",    fmtd(hor.az_deg,  2) + " deg");
    row("Altitude",   fmtd(hor.alt_deg, 2) + " deg");
    row("Airmass",    fmtd(X, 3));
    hline(out, viewport_w, '-');
    row("Telescope",  session.telescope().name);
    row("Aperture",   fmtd(session.telescope().aperture_mm, 1) + " mm");
    row("F-ratio",    "f/" + fmtd(session.telescope().fRatio(), 1));
    row("Pixel scale",fmtd(session.telescope().pixelScale(), 3) + " \"/px");
    hline(out, viewport_w, '-');
    row("Seeing",     fmtd(seeing, 2) + " arcsec FWHM");
    row("Extinction", fmtd(ext,    3) + " mag");
    row("Sky bg",     fmtd(session.atmosphere().skyBackground(hor.alt_deg), 1)
                      + " mag/arcsec^2");
    row("SNR",        fmtd(target_snr, 1));
    hline(out, viewport_w, '=');
}

// -----------------------------------------------------------------------
// renderStarReadout
// -----------------------------------------------------------------------
void ConsoleRenderer::renderStarReadout(std::ostream& out,
                                         const Star& star,
                                         const ObservingSession& session,
                                         double exposure_s) const {
    double snr = session.snr(star.position, star.v_magnitude, exposure_s);
    Horizontal hor = session.toHorizontal(star.position);

    hline(out, viewport_w, '=');
    out << "| STAR CATALOG RECORD"
        << std::setw(viewport_w - 22) << "" << "|\n";
    hline(out, viewport_w, '-');

    auto row = [&](const std::string& lbl, const std::string& val) {
        out << "| " << std::left << std::setw(22) << lbl
            << " : " << std::left << std::setw(viewport_w - 29) << val
            << "|\n";
    };
    auto fmtd = [](double v, int prec = 2) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(prec) << v;
        return ss.str();
    };

    row("Name",        star.name.empty() ? "(unnamed)" : star.name);
    row("Catalog ID",  std::to_string(star.id));
    row("RA (J2000)",  fmtd(star.position.ra_deg,  4) + " deg");
    row("Dec (J2000)", fmtd(star.position.dec_deg, 4) + " deg");
    row("V magnitude", fmtd(star.v_magnitude, 2));
    row("Abs magnitude", fmtd(star.abs_magnitude, 2));
    row("Distance",    fmtd(star.distance_pc, 2) + " pc  ("
                       + fmtd(star.distance_pc * 3.2616, 2) + " ly)");
    row("Parallax",    fmtd(star.parallax_mas, 3) + " mas");
    row("Spectral",    [&]() -> std::string {
        switch (star.spectral_class) {
            case SpectralClass::O:  return "O";
            case SpectralClass::B:  return "B";
            case SpectralClass::A:  return "A";
            case SpectralClass::F:  return "F";
            case SpectralClass::G:  return "G";
            case SpectralClass::K:  return "K";
            case SpectralClass::M:  return "M";
            default:                return "?";
        }
    }());
    row("Variable",    star.is_variable ? "Yes" : "No");
    row("Altitude",    fmtd(hor.alt_deg, 2) + " deg");
    row("Azimuth",     fmtd(hor.az_deg,  2) + " deg");
    row("SNR (" + fmtd(exposure_s, 0) + "s)", fmtd(snr, 1));
    hline(out, viewport_w, '=');
}

} // namespace parallax

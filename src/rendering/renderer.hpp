#pragma once
// rendering/renderer.hpp - Console-based scientific terminal renderer
//
// Renders the starfield to an ASCII/ANSI terminal using a retro
// observatory console aesthetic.  Works on any POSIX terminal.

#include "rendering/star_field.hpp"
#include "observatory/observer.hpp"
#include "universe/star_catalog.hpp"
#include <string>
#include <vector>
#include <ostream>

namespace parallax {

// -----------------------------------------------------------------------
// ConsoleRenderer
// -----------------------------------------------------------------------
class ConsoleRenderer {
public:
    /// Width/height of the rendered viewport (characters)
    int viewport_w{80};
    int viewport_h{24};

    // -----------------------------------------------------------------------
    // Text-mode starfield
    // -----------------------------------------------------------------------

    /// Render a starfield to an output stream using ASCII art.
    /// Stars are drawn using brightness-mapped glyphs:
    ///   ● ✦ * · .   (brightest to faintest)
    void renderStarField(std::ostream& out,
                          const std::vector<StarRenderRecord>& stars,
                          const std::string& title = "") const;

    // -----------------------------------------------------------------------
    // Instrument readout panel
    // -----------------------------------------------------------------------

    /// Print a formatted instrument status panel.
    void renderStatusPanel(std::ostream& out,
                            const ObservingSession& session,
                            const Equatorial& target,
                            double target_snr) const;

    // -----------------------------------------------------------------------
    // Data readout for a star
    // -----------------------------------------------------------------------

    /// Print star data in observatory terminal style.
    void renderStarReadout(std::ostream& out, const Star& star,
                            const ObservingSession& session,
                            double exposure_s = 60.0) const;

private:
    /// Map brightness [0..1] to a glyph character.
    static char brightnessGlyph(double brightness);

    /// Draw a horizontal separator line.
    static void hline(std::ostream& out, int w, char c = '-');
};

} // namespace parallax

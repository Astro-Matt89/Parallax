/// @file constellations.cpp
/// @brief Constellation overlay implementation — CSV loading, HIP resolution,
///        per-frame transform, line + label submission.
///
/// CRITICAL: All screen positions are computed via the SHARED function
/// Coordinates::project_radec_to_screen() — the SAME function used by
/// the starfield renderer. This guarantees lines connect to their stars.

#include "overlay/constellations.hpp"

#include "core/logger.hpp"

#include <cmath>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

namespace parallax::overlay
{

// =================================================================
// CSV loading
// =================================================================

bool Constellations::load(const std::filesystem::path& lines_path,
                          const std::filesystem::path& names_path)
{
    if (!load_lines(lines_path))
    {
        return false;
    }
    load_names(names_path);

    PLX_CORE_INFO("Constellations loaded: {} constellations, {} segments",
                  get_constellation_count(), get_segment_count());
    return true;
}

bool Constellations::load_lines(const std::filesystem::path& path)
{
    std::ifstream file{path};
    if (!file.is_open())
    {
        PLX_CORE_WARN("Cannot open constellation lines: {}", path.string());
        return false;
    }

    std::unordered_map<std::string, u32> abbr_to_idx;

    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty() || line[0] == '#')
        {
            continue;
        }

        std::istringstream ss{line};
        std::string abbr;
        std::string hip1_str;
        std::string hip2_str;

        if (!std::getline(ss, abbr, ',') ||
            !std::getline(ss, hip1_str, ',') ||
            !std::getline(ss, hip2_str))
        {
            continue;
        }

        u32 hip1 = 0;
        u32 hip2 = 0;
        try
        {
            hip1 = static_cast<u32>(std::stoul(hip1_str));
            hip2 = static_cast<u32>(std::stoul(hip2_str));
        }
        catch (...)
        {
            continue;
        }

        auto it = abbr_to_idx.find(abbr);
        if (it == abbr_to_idx.end())
        {
            abbr_to_idx[abbr] = static_cast<u32>(m_constellations.size());
            m_constellations.push_back(ConstellationData{
                .abbreviation = abbr,
                .name = abbr,
                .segments = {{hip1, hip2}},
            });
        }
        else
        {
            m_constellations[it->second].segments.emplace_back(hip1, hip2);
        }
    }

    return !m_constellations.empty();
}

void Constellations::load_names(const std::filesystem::path& path)
{
    std::ifstream file{path};
    if (!file.is_open())
    {
        PLX_CORE_WARN("Cannot open constellation names: {}", path.string());
        return;
    }

    std::unordered_map<std::string, u32> abbr_to_idx;
    for (u32 i = 0; i < static_cast<u32>(m_constellations.size()); ++i)
    {
        abbr_to_idx[m_constellations[i].abbreviation] = i;
    }

    std::string line;
    u32 matched = 0;
    while (std::getline(file, line))
    {
        if (line.empty() || line[0] == '#')
        {
            continue;
        }

        std::istringstream ss{line};
        std::string abbr;
        std::string name;

        if (!std::getline(ss, abbr, ',') || !std::getline(ss, name))
        {
            continue;
        }

        auto it = abbr_to_idx.find(abbr);
        if (it != abbr_to_idx.end())
        {
            m_constellations[it->second].name = name;
            ++matched;
        }
    }

    PLX_CORE_INFO("Constellation names matched: {}/{}", matched, m_constellations.size());
}

// =================================================================
// HIP resolution
// =================================================================

void Constellations::resolve_stars(std::span<const catalog::StarEntry> catalog)
{
    m_catalog = catalog;
    m_hip_to_index.clear();
    m_hip_to_index.reserve(catalog.size());

    for (u32 i = 0; i < static_cast<u32>(catalog.size()); ++i)
    {
        m_hip_to_index[catalog[i].catalog_id] = i;
    }

    u32 resolved = 0;
    u32 missing = 0;
    for (const auto& c : m_constellations)
    {
        for (const auto& [h1, h2] : c.segments)
        {
            if (m_hip_to_index.contains(h1)) { ++resolved; } else { ++missing; }
            if (m_hip_to_index.contains(h2)) { ++resolved; } else { ++missing; }
        }
    }

    PLX_CORE_INFO("Constellation HIP resolution: {} resolved, {} missing",
                  resolved, missing);

    // Diagnostic: verify Orion reference stars via shared projection
    PLX_CORE_INFO("=== Orion Reference Star Verification ===");
    auto verify = [&](u32 hip, const char* name, f64 expected_ra_deg, f64 expected_dec_deg)
    {
        auto it = m_hip_to_index.find(hip);
        if (it != m_hip_to_index.end())
        {
            const auto& star = catalog[it->second];
            const f64 ra_deg = star.ra * astro_constants::kRadToDeg;
            const f64 dec_deg = star.dec * astro_constants::kRadToDeg;
            PLX_CORE_INFO("  HIP {} ({}): RA={:.2f} Dec={:.2f} (expect {:.2f}, {:.2f}) {}",
                          hip, name, ra_deg, dec_deg, expected_ra_deg, expected_dec_deg,
                          (std::abs(ra_deg - expected_ra_deg) < 1.0 &&
                           std::abs(dec_deg - expected_dec_deg) < 1.0) ? "OK" : "MISMATCH");
        }
        else
        {
            PLX_CORE_WARN("  HIP {} ({}): NOT FOUND in catalog", hip, name);
        }
    };

    verify(27989, "Betelgeuse", 88.79, 7.41);
    verify(24436, "Rigel",      78.63, -8.20);
    verify(25336, "Bellatrix",  81.28, 6.35);
}

std::optional<u32> Constellations::resolve_hip(u32 hip_id) const
{
    auto it = m_hip_to_index.find(hip_id);
    if (it != m_hip_to_index.end())
    {
        return it->second;
    }
    return std::nullopt;
}

// =================================================================
// Per-frame update
//
// ALL screen positions computed via Coordinates::project_radec_to_screen()
// — the SAME function the starfield uses. One function. Shared by all.
//
// Label centroids are averaged from PROJECTED screen positions,
// NOT from averaged RA/Dec then projected.
// =================================================================

void Constellations::update(const rendering::Camera& camera,
                            const astro::ObserverLocation& observer,
                            f64 lst_rad,
                            rendering::LineRenderer& lines,
                            ui::BitmapFont& font,
                            VkExtent2D viewport)
{
    if (!m_visible || m_constellations.empty() || m_catalog.empty())
    {
        return;
    }

    // Extract camera params — SAME values the starfield uses
    const auto pointing = camera.get_pointing();
    const f64 fov_rad = camera.get_fov_rad();

    const f32 vw = static_cast<f32>(viewport.width);
    const f32 vh = static_cast<f32>(viewport.height);

    for (const auto& constellation : m_constellations)
    {
        // Accumulators for label centroid — from PROJECTED screen positions
        f32 sum_sx = 0.0f;
        f32 sum_sy = 0.0f;
        u32 screen_count = 0;

        for (const auto& [hip1, hip2] : constellation.segments)
        {
            const auto idx1 = resolve_hip(hip1);
            const auto idx2 = resolve_hip(hip2);
            if (!idx1.has_value() || !idx2.has_value())
            {
                continue;
            }

            const auto& star1 = m_catalog[idx1.value()];
            const auto& star2 = m_catalog[idx2.value()];

            // RA/Dec → screen NDC via THE SAME shared function as starfield
            // star.ra and star.dec are already in RADIANS (from catalog loader)
            const auto s1 = astro::Coordinates::project_radec_to_screen(
                star1.ra, star1.dec, observer, lst_rad, pointing, fov_rad);

            const auto s2 = astro::Coordinates::project_radec_to_screen(
                star2.ra, star2.dec, observer, lst_rad, pointing, fov_rad);

            if (!s1.has_value() || !s2.has_value())
            {
                continue;
            }

            // Submit line — NDC positions go directly to LineRenderer
            lines.add_line(s1.value(), s2.value(), kLineColor);

            // Accumulate PROJECTED screen positions for label centroid
            sum_sx += s1->x + s2->x;
            sum_sy += s1->y + s2->y;
            screen_count += 2;
        }

        // Draw abbreviation label at centroid of PROJECTED visible endpoints
        if (screen_count >= 2)
        {
            // Average NDC position
            const f32 cx_ndc = sum_sx / static_cast<f32>(screen_count);
            const f32 cy_ndc = sum_sy / static_cast<f32>(screen_count);

            // NDC [-1,1] → pixel coordinates
            // X: -1 = left edge, +1 = right edge
            // Y: In Vulkan NDC, -1 = top, +1 = bottom (Y already flipped
            //    by horizontal_to_screen which negates proj_y for Vulkan)
            // So: pixel_x = (ndc_x + 1) / 2 * width
            //     pixel_y = (ndc_y + 1) / 2 * height
            const f32 px = (cx_ndc + 1.0f) * 0.5f * vw;
            const f32 py = (cy_ndc + 1.0f) * 0.5f * vh;

            // Center the label (8 px per char at scale 1.0)
            const f32 label_w = static_cast<f32>(
                constellation.abbreviation.size()) * 8.0f * kLabelScale;
            const f32 label_h = 16.0f * kLabelScale;

            font.draw_text(constellation.abbreviation,
                           px - label_w * 0.5f,
                           py - label_h * 0.5f,
                           kLabelScale, kLabelColor);
        }
    }
}

// =================================================================
// Visibility
// =================================================================

void Constellations::set_visible(bool visible) { m_visible = visible; }
void Constellations::toggle_visible() { m_visible = !m_visible; }
bool Constellations::is_visible() const { return m_visible; }

// =================================================================
// Stats
// =================================================================

u32 Constellations::get_constellation_count() const
{
    return static_cast<u32>(m_constellations.size());
}

u32 Constellations::get_segment_count() const
{
    u32 total = 0;
    for (const auto& c : m_constellations)
    {
        total += static_cast<u32>(c.segments.size());
    }
    return total;
}

} // namespace parallax::overlay
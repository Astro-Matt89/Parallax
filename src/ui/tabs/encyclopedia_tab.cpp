#include "ui/tabs/encyclopedia_tab.hpp"

#include "core/logger.hpp"
#include "knowledge/knowledge_database.hpp"
#include "knowledge/knowledge_level.hpp"
#include "knowledge/property_registry.hpp"
#include "rendering/line_renderer.hpp"
#include "ui/font.hpp"
#include "ui/tabs/tab_render_helpers.hpp"
#include "ui/widgets.hpp"
#include "universe/object_id.hpp"
#include "universe/universe.hpp"
#include "vulkan/swapchain.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <format>
#include <optional>
#include <string_view>
#include <utility>

namespace parallax::ui::tabs
{
    namespace
    {
        constexpr u32 kPaddingX       = 8;
        constexpr u32 kHeaderH        = 24;
        constexpr u32 kRowH           = 18;
        constexpr u32 kSectionHeaderH = 16;
        constexpr u32 kSectionGap     = 8;
        constexpr u32 kBorderWidth    = 1;
        constexpr u32 kLeftPanePct    = 30;
        constexpr u32 kActionRowH     = 26;
        constexpr u32 kSearchBoxH     = 22;
        constexpr u32 kFilterRowH     = 22;
        constexpr u32 kSortRowH       = 22;

        constexpr f32 kGlyphW = 8.0f;

        // TODO: deduplicate shell colour palette.
        constexpr Vec4f kBackgroundColor{0.02f, 0.06f, 0.02f, 1.0f};
        constexpr Vec4f kBorderColor = widget_colors::kBorderBright;
        constexpr Vec3f kHeaderColor = widget_colors::kTextBright;
        constexpr Vec3f kContentColor = widget_colors::kTextBright;
        constexpr Vec3f kDimColor = widget_colors::kTextDim;
        constexpr Vec3f kInactiveColor{0.0f, 0.45f, 0.0f};
        constexpr Vec4f kHoverColor = widget_colors::kHighlight;
        constexpr Vec4f kSelectedColor{0.0f, 0.35f, 0.0f, 0.45f};
        constexpr Vec4f kButtonInactive{0.0f, 0.20f, 0.0f, 0.45f};
        constexpr Vec4f kButtonDisabled{0.0f, 0.10f, 0.0f, 0.35f};
        constexpr Vec4f kWarningBadge{1.0f, 0.85f, 0.2f, 1.0f};

        struct Layout
        {
            shell::ViewportRect left_rect{};
            shell::ViewportRect right_rect{};
            shell::ViewportRect divider_rect{};
            std::array<shell::ViewportRect, 5> filter_buttons{};
            shell::ViewportRect level_button{};
            shell::ViewportRect search_box{};
            shell::ViewportRect sort_button{};
            shell::ViewportRect results_rect{};
            shell::ViewportRect action_row{};
            std::array<shell::ViewportRect, 3> action_buttons{};
        };

        [[nodiscard]] Vec2f pixel_to_ndc(const Vec2f px, const Vec2f viewport)
        {
            return {
                (px.x / viewport.x) * 2.0f - 1.0f,
                (px.y / viewport.y) * 2.0f - 1.0f
            };
        }

        void draw_filled_rect(rendering::LineRenderer& lines,
                              const shell::ViewportRect& rect,
                              const Vec4f color,
                              const Vec2f viewport)
        {
            if (!rect.is_valid())
            {
                return;
            }

            const f32 x0 = static_cast<f32>(rect.x);
            const f32 x1 = static_cast<f32>(rect.right());
            for (u32 y = rect.y; y < rect.bottom(); ++y)
            {
                const f32 py = static_cast<f32>(y) + 0.5f;
                lines.add_line(pixel_to_ndc({x0, py}, viewport),
                               pixel_to_ndc({x1, py}, viewport),
                               color);
            }
        }

        [[nodiscard]] shell::ViewportRect make_rect(i32 x, i32 y, i32 width, i32 height)
        {
            return {
                static_cast<u32>(std::max(0, x)),
                static_cast<u32>(std::max(0, y)),
                static_cast<u32>(std::max(0, width)),
                static_cast<u32>(std::max(0, height))
            };
        }

        [[nodiscard]] const char* min_level_text(const EncyclopediaTab::MinLevel level)
        {
            switch (level)
            {
                case EncyclopediaTab::MinLevel::L1: return "L1+";
                case EncyclopediaTab::MinLevel::L2: return "L2+";
                case EncyclopediaTab::MinLevel::L3: return "L3+";
                case EncyclopediaTab::MinLevel::L4: return "L4+";
            }
            return "L1+";
        }

        [[nodiscard]] const char* sort_key_text(const EncyclopediaTab::SortKey key)
        {
            switch (key)
            {
                case EncyclopediaTab::SortKey::Name:          return "Name";
                case EncyclopediaTab::SortKey::Distance:      return "Distance";
                case EncyclopediaTab::SortKey::Magnitude:     return "Magnitude";
                case EncyclopediaTab::SortKey::DiscoveryDate: return "Discovery Date";
            }
            return "Name";
        }

        [[nodiscard]] knowledge::KnowledgeLevel min_level_to_knowledge(const EncyclopediaTab::MinLevel level)
        {
            switch (level)
            {
                case EncyclopediaTab::MinLevel::L1: return knowledge::KnowledgeLevel::Detected;
                case EncyclopediaTab::MinLevel::L2: return knowledge::KnowledgeLevel::Classified;
                case EncyclopediaTab::MinLevel::L3: return knowledge::KnowledgeLevel::Characterized;
                case EncyclopediaTab::MinLevel::L4: return knowledge::KnowledgeLevel::Detailed;
            }
            return knowledge::KnowledgeLevel::Detected;
        }

        [[nodiscard]] std::string knowledge_level_to_text(const knowledge::KnowledgeLevel level)
        {
            switch (level)
            {
                case knowledge::KnowledgeLevel::Detected:      return "L1";
                case knowledge::KnowledgeLevel::Classified:    return "L2";
                case knowledge::KnowledgeLevel::Characterized: return "L3";
                case knowledge::KnowledgeLevel::Detailed:      return "L4";
                case knowledge::KnowledgeLevel::Resolved:      return "L5";
                case knowledge::KnowledgeLevel::FullyMapped:   return "L6";
                default:                                        return "L0";
            }
        }

        [[nodiscard]] std::string_view measurement_technique_text(const knowledge::MeasurementTechnique technique)
        {
            using Technique = knowledge::MeasurementTechnique;
            switch (technique)
            {
                case Technique::BroadbandPhotometry: return "photometry";
                case Technique::PrecisionPhotometry: return "precision photometry";
                case Technique::SpectroscopyLowRes:  return "spectroscopy";
                case Technique::SpectroscopyHighRes: return "high-res spectroscopy";
                case Technique::RadialVelocity:      return "radial velocity";
                case Technique::Astrometry:          return "astrometry";
                case Technique::Interferometry:      return "interferometry";
                case Technique::PolarimetryLinear:   return "polarimetry";
                case Technique::Coronagraphy:        return "coronagraphy";
                case Technique::RadioObservation:    return "radio";
                case Technique::XRayObservation:     return "x-ray";
                case Technique::NeutrinoDetection:   return "neutrino";
                case Technique::GravitationalWave:   return "gravitational wave";
                case Technique::DirectNeuralImaging: return "neural imaging";
                case Technique::None:                return "unknown";
            }
            return "unknown";
        }

        [[nodiscard]] std::optional<double> get_catalog_property_value(const universe::CelestialObject& object,
                                                                        const std::string_view property_name)
        {
            if (property_name == "ra")
            {
                return object.ra;
            }
            if (property_name == "dec")
            {
                return object.dec;
            }
            if (property_name == "mag_v")
            {
                return static_cast<double>(object.mag_v);
            }
            if (property_name == "color_bv")
            {
                return static_cast<double>(object.color_bv);
            }
            if (property_name == "size_arcmin")
            {
                if (const auto* dso = std::get_if<universe::DsoData>(&object.data))
                {
                    return static_cast<double>(dso->size_arcmin);
                }
            }
            if (property_name == "distance_au")
            {
                if (const auto* solar = std::get_if<universe::SolarSystemData>(&object.data))
                {
                    return static_cast<double>(solar->distance_au);
                }
            }
            if (property_name == "angular_diameter_arcsec")
            {
                if (const auto* solar = std::get_if<universe::SolarSystemData>(&object.data))
                {
                    return static_cast<double>(solar->apparent_diameter_arcsec);
                }
            }
            if (property_name == "parallax_mas")
            {
                if (const auto* star = std::get_if<universe::StarData>(&object.data))
                {
                    return static_cast<double>(star->parallax_mas);
                }
            }
            if (property_name == "distance_pc")
            {
                if (const auto* star = std::get_if<universe::StarData>(&object.data))
                {
                    return static_cast<double>(star->distance_pc);
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] char status_glyph(const universe::CelestialObject& object,
                                        const knowledge::KnowledgeDatabase& knowledge)
        {
            if (object.is_real())
            {
                return '*';
            }

            return knowledge.is_confirmed(object.id) ? '+' : '?';
        }

        [[nodiscard]] std::string status_text(const universe::CelestialObject& object,
                                              const knowledge::KnowledgeDatabase& knowledge)
        {
            if (object.is_real())
            {
                return "HISTORICAL";
            }

            return knowledge.is_confirmed(object.id) ? "DISCOVERED" : "UNCONFIRMED";
        }

        [[nodiscard]] Vec4f status_badge_color(const universe::CelestialObject& object,
                                               const knowledge::KnowledgeDatabase& knowledge)
        {
            if (object.is_real())
            {
                return Vec4f{kInactiveColor.x, kInactiveColor.y, kInactiveColor.z, 1.0f};
            }

            return knowledge.is_confirmed(object.id) ?
                Vec4f{kHeaderColor.x, kHeaderColor.y, kHeaderColor.z, 1.0f}
                : kWarningBadge;
        }

        [[nodiscard]] std::string default_designation(const universe::CelestialObject& object,
                                                      const universe::Universe& universe_ref)
        {
            const std::string_view name = universe_ref.get_name(object.id);
            if (!name.empty())
            {
                return std::string{name};
            }

            const u64 source_id = universe::decode_source_id(object.id);
            switch (object.type)
            {
                case universe::ObjectType::Star:
                {
                    if (const auto* star = std::get_if<universe::StarData>(&object.data))
                    {
                        if (star->hip_id != 0)
                        {
                            return std::format("HIP {}", star->hip_id);
                        }
                    }
                    return std::format("HIP {}", source_id);
                }
                case universe::ObjectType::DeepSkyObject:
                case universe::ObjectType::Galaxy:
                    return std::format("M{}", source_id);
                case universe::ObjectType::SolarSystemBody:
                    return std::format("Body {}", source_id);
                case universe::ObjectType::ProceduralStar:
                case universe::ObjectType::ProceduralDso:
                    return std::format("PRC {:016X}", object.id);
                default:
                    return std::format("ID {}", object.id);
            }
        }

        [[nodiscard]] std::string subtitle_for(const universe::CelestialObject& object)
        {
            switch (object.type)
            {
                case universe::ObjectType::Star:
                    return "Star";
                case universe::ObjectType::ProceduralStar:
                    return "Procedural";
                case universe::ObjectType::DeepSkyObject:
                case universe::ObjectType::Galaxy:
                case universe::ObjectType::ProceduralDso:
                {
                    if (const auto* dso = std::get_if<universe::DsoData>(&object.data))
                    {
                        return catalog::dso_type_name(dso->dso_type);
                    }
                    return "DSO";
                }
                case universe::ObjectType::SolarSystemBody:
                    return "Solar System";
                default:
                    return "Unknown";
            }
        }

        [[nodiscard]] Layout build_layout(const shell::ViewportRect& rect)
        {
            Layout layout{};

            const i32 divider_w = static_cast<i32>(kBorderWidth);
            const i32 left_w = static_cast<i32>((rect.width * kLeftPanePct) / 100);
            const i32 right_w = static_cast<i32>(rect.width) - left_w - divider_w;

            layout.left_rect = make_rect(static_cast<i32>(rect.x), static_cast<i32>(rect.y), left_w,
                                         static_cast<i32>(rect.height));
            layout.divider_rect = make_rect(static_cast<i32>(layout.left_rect.right()), static_cast<i32>(rect.y),
                                            divider_w, static_cast<i32>(rect.height));
            layout.right_rect = make_rect(static_cast<i32>(layout.divider_rect.right()), static_cast<i32>(rect.y),
                                          right_w, static_cast<i32>(rect.height));

            const i32 content_x = static_cast<i32>(layout.left_rect.x + kPaddingX);
            const i32 content_w = static_cast<i32>(layout.left_rect.width) - static_cast<i32>(2 * kPaddingX);
            i32 cursor_y = static_cast<i32>(layout.left_rect.y + kPaddingX);

            const i32 filter_w = std::max(1, content_w / 5);
            for (i32 i = 0; i < 5; ++i)
            {
                const i32 x = content_x + i * filter_w;
                const i32 w = (i == 4) ? (content_w - i * filter_w) : filter_w;
                layout.filter_buttons[static_cast<std::size_t>(i)] =
                    make_rect(x, cursor_y, w, static_cast<i32>(kFilterRowH));
            }
            cursor_y += static_cast<i32>(kFilterRowH + 4);

            layout.level_button = make_rect(content_x, cursor_y, content_w, static_cast<i32>(kFilterRowH));
            cursor_y += static_cast<i32>(kFilterRowH + 4);

            layout.search_box = make_rect(content_x, cursor_y, content_w, static_cast<i32>(kSearchBoxH));
            cursor_y += static_cast<i32>(kSearchBoxH + 4);

            layout.sort_button = make_rect(content_x, cursor_y, content_w, static_cast<i32>(kSortRowH));
            cursor_y += static_cast<i32>(kSortRowH + 4);

            layout.results_rect = make_rect(content_x, cursor_y,
                                            content_w,
                                            static_cast<i32>(layout.left_rect.bottom()) - cursor_y -
                                                static_cast<i32>(kPaddingX));

            const i32 action_y = static_cast<i32>(layout.right_rect.bottom()) -
                                 static_cast<i32>(kPaddingX) -
                                 static_cast<i32>(kActionRowH);
            const i32 action_x = static_cast<i32>(layout.right_rect.x + kPaddingX);
            const i32 action_w = static_cast<i32>(layout.right_rect.width) - static_cast<i32>(2 * kPaddingX);
            layout.action_row = make_rect(action_x, action_y, action_w, static_cast<i32>(kActionRowH));

            const i32 button_w = std::max(1, action_w / 3);
            for (i32 i = 0; i < 3; ++i)
            {
                const i32 x = action_x + i * button_w;
                const i32 w = (i == 2) ? (action_w - i * button_w) : button_w;
                layout.action_buttons[static_cast<std::size_t>(i)] =
                    make_rect(x, action_y, w, static_cast<i32>(kActionRowH));
            }

            return layout;
        }

        void apply_viewport(VkCommandBuffer cmd, const shell::ViewportRect& rect)
        {
            VkViewport vk_viewport{};
            vk_viewport.x = static_cast<f32>(rect.x);
            vk_viewport.y = static_cast<f32>(rect.y);
            vk_viewport.width = static_cast<f32>(rect.width);
            vk_viewport.height = static_cast<f32>(rect.height);
            vk_viewport.minDepth = 0.0f;
            vk_viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &vk_viewport);

            VkRect2D scissor{};
            scissor.offset = {static_cast<i32>(rect.x), static_cast<i32>(rect.y)};
            scissor.extent = {rect.width, rect.height};
            vkCmdSetScissor(cmd, 0, 1, &scissor);
        }
    }

    EncyclopediaTab::EncyclopediaTab(BitmapFont& font,
                                     rendering::LineRenderer& line_renderer,
                                     vulkan::Swapchain& swapchain,
                                     const universe::Universe& universe,
                                     const knowledge::KnowledgeDatabase& knowledge,
                                     const observation::DataArchive& archive,
                                     Selection& selection)
        : m_font(font)
        , m_line_renderer(line_renderer)
        , m_swapchain(swapchain)
        , m_universe(universe)
        , m_knowledge(knowledge)
        , m_archive(archive)
        , m_selection(selection)
    {
    }

    void EncyclopediaTab::set_shell_hooks(ShellHooks hooks)
    {
        m_hooks = std::move(hooks);
    }

    void EncyclopediaTab::update(f64 delta_time)
    {
        static_cast<void>(delta_time);

        sync_selection_from_global(m_last_rect);
        rebuild_cache(m_last_rect);
        rebuild_detail_cache();

        // TODO(Sprint 10+): cache invalidation keyed by data versions/filter-change only.
    }

    void EncyclopediaTab::render(VkCommandBuffer cmd, const shell::ViewportRect& rect)
    {
        if (!rect.is_valid())
        {
            return;
        }

        m_last_rect = rect;
        apply_viewport(cmd, rect);

        const Layout layout = build_layout(rect);
        const VkExtent2D extent = m_swapchain.get_extent();
        const Vec2f viewport{static_cast<f32>(extent.width), static_cast<f32>(extent.height)};

        m_line_renderer.begin_frame();

        draw_filled_rect(m_line_renderer, rect, kBackgroundColor, viewport);
        draw_filled_rect(m_line_renderer, layout.divider_rect, kBorderColor, viewport);

        constexpr std::array<const char*, 5> kFilterLabels = {
            "Stars", "DSOs", "Solar", "Discovered", "Candidates"
        };

        const std::array<bool, 5> filter_values = {
            m_filters.stars, m_filters.dsos, m_filters.solar, m_filters.discovered, m_filters.candidates
        };

        for (i32 i = 0; i < 5; ++i)
        {
            const shell::ViewportRect button = layout.filter_buttons[static_cast<std::size_t>(i)];
            const bool enabled = filter_values[static_cast<std::size_t>(i)];
            draw_filled_rect(m_line_renderer, button, enabled ? kSelectedColor : kButtonInactive, viewport);
            const std::string text = std::format("{} {}", enabled ? "+" : "-", kFilterLabels[static_cast<std::size_t>(i)]);
            m_font.draw_text(text, static_cast<f32>(button.x + 3), static_cast<f32>(button.y + 3),
                             1.0f, enabled ? kHeaderColor : kDimColor);
        }

        draw_filled_rect(m_line_renderer, layout.level_button, kButtonInactive, viewport);
        m_font.draw_text(std::format("Min level: {}", min_level_text(m_min_level)),
                         static_cast<f32>(layout.level_button.x + 4),
                         static_cast<f32>(layout.level_button.y + 3),
                         1.0f,
                         kContentColor);

        draw_filled_rect(m_line_renderer, layout.search_box,
                         m_search_focused ? kSelectedColor : kButtonInactive,
                         viewport);
        if (m_search_query.empty())
        {
            // TODO(Task 9.11): wire shell text-input routing for editable search.
            m_font.draw_text("Search: (search disabled - Task 9.11)",
                             static_cast<f32>(layout.search_box.x + 4),
                             static_cast<f32>(layout.search_box.y + 3),
                             1.0f,
                             kDimColor);
        }
        else
        {
            m_font.draw_text(std::format("Search: {}", m_search_query),
                             static_cast<f32>(layout.search_box.x + 4),
                             static_cast<f32>(layout.search_box.y + 3),
                             1.0f,
                             kContentColor);
        }

        draw_filled_rect(m_line_renderer, layout.sort_button, kButtonInactive, viewport);
        m_font.draw_text(std::format("Sort: {} {}", sort_key_text(m_sort_key),
                                     m_sort_dir == SortDir::Asc ? "^" : "v"),
                         static_cast<f32>(layout.sort_button.x + 4),
                         static_cast<f32>(layout.sort_button.y + 3),
                         1.0f,
                         kContentColor);

        const i32 rows_visible = visible_row_count(rect);
        i32 y = static_cast<i32>(layout.results_rect.y);

        for (i32 local_row = 0; local_row < rows_visible; ++local_row)
        {
            const i32 absolute_row = m_scroll_row + local_row;
            if (absolute_row >= static_cast<i32>(m_cached_rows.size()))
            {
                break;
            }

            const shell::ViewportRect row_rect = make_rect(
                static_cast<i32>(layout.results_rect.x),
                y,
                static_cast<i32>(layout.results_rect.width),
                static_cast<i32>(kRowH));

            const auto& row = m_cached_rows[static_cast<std::size_t>(absolute_row)];
            if (m_selected_object.has_value() && m_selected_object.value() == row.id)
            {
                draw_filled_rect(m_line_renderer, row_rect, kSelectedColor, viewport);
            }
            else if (m_hovered_row == absolute_row)
            {
                draw_filled_rect(m_line_renderer, row_rect, kHoverColor, viewport);
            }

            const std::string row_label = std::format("{} {}   {}", row.status_glyph, row.designation, row.subtitle);
            m_font.draw_text(row_label,
                             static_cast<f32>(row_rect.x + 4),
                             static_cast<f32>(row_rect.y + 1),
                             1.0f,
                             kContentColor);

            y += static_cast<i32>(kRowH);
        }

        const f32 right_x = static_cast<f32>(layout.right_rect.x + kPaddingX);
        f32 right_y = static_cast<f32>(layout.right_rect.y + kPaddingX);

        if (!m_selected_object.has_value())
        {
            const std::string text = "Select an object";
            const f32 text_w = static_cast<f32>(text.size()) * kGlyphW;
            const f32 center_x = static_cast<f32>(layout.right_rect.x) +
                                 (static_cast<f32>(layout.right_rect.width) - text_w) * 0.5f;
            const f32 center_y = static_cast<f32>(layout.right_rect.y) +
                                 static_cast<f32>(layout.right_rect.height) * 0.5f;
            m_font.draw_text(text, center_x, center_y, 1.0f, kDimColor);
        }
        else
        {
            m_font.draw_text(m_detail_cache.designation, right_x, right_y, 1.0f, kHeaderColor);

            const f32 badge_x = right_x +
                                (static_cast<f32>(m_detail_cache.designation.size()) + 2.0f) * kGlyphW;
            const shell::ViewportRect badge_rect = make_rect(
                static_cast<i32>(badge_x),
                static_cast<i32>(right_y - 1.0f),
                static_cast<i32>((m_detail_cache.status_text.size() + m_detail_cache.status_level.size() + 4) * kGlyphW),
                static_cast<i32>(kRowH));
            draw_filled_rect(m_line_renderer, badge_rect, m_detail_cache.status_bg, viewport);
            m_font.draw_text(std::format("{} {}", m_detail_cache.status_text, m_detail_cache.status_level),
                             badge_x + 3.0f,
                             right_y + 1.0f,
                             1.0f,
                             {0.0f, 0.08f, 0.0f});
            right_y += static_cast<f32>(kHeaderH);

            const auto draw_section = [&](const char* title, const std::vector<PropertyLine>& lines)
            {
                if (lines.empty())
                {
                    return;
                }

                m_font.draw_text(title, right_x, right_y, 1.0f, kDimColor);
                right_y += static_cast<f32>(kSectionHeaderH);

                for (const auto& line : lines)
                {
                    m_font.draw_text(std::format("{} {}", line.label, line.value),
                                     right_x,
                                     right_y,
                                     1.0f,
                                     line.dim_hint ? kDimColor : kContentColor);
                    right_y += static_cast<f32>(kRowH);
                }

                right_y += static_cast<f32>(kSectionGap);
            };

            draw_section("POSITION", m_detail_cache.position_rows);
            draw_section("PHOTOMETRY", m_detail_cache.photometry_rows);
            draw_section("TYPE", m_detail_cache.type_rows);
            draw_section("DISCOVERY", m_detail_cache.discovery_rows);
            draw_section("TO DISCOVER", m_detail_cache.discover_rows);

            m_font.draw_text(std::format("Observations: {}", m_detail_cache.observation_count),
                             right_x,
                             std::min(right_y, static_cast<f32>(layout.action_row.y - kRowH - 4)),
                             1.0f,
                             kContentColor);
        }

        constexpr std::array<const char*, 3> kActions{"OBSERVE", "LOCATE", "TRACK"};
        const bool enabled = m_selected_object.has_value();
        for (i32 i = 0; i < 3; ++i)
        {
            const shell::ViewportRect button = layout.action_buttons[static_cast<std::size_t>(i)];
            draw_filled_rect(m_line_renderer, button, enabled ? kSelectedColor : kButtonDisabled, viewport);
            m_font.draw_text(kActions[static_cast<std::size_t>(i)],
                             static_cast<f32>(button.x + 4),
                             static_cast<f32>(button.y + 5),
                             1.0f,
                             enabled ? kHeaderColor : kDimColor);
        }

        shell::apply_full_viewport_pane_scissor(cmd, extent, rect);
        m_line_renderer.render(cmd);
        m_font.render(cmd, extent);
    }

    void EncyclopediaTab::on_input(const shell::InputEvent& event, const shell::ViewportRect& rect)
    {
        if (!rect.contains(static_cast<i32>(event.mouse_pos.x), static_cast<i32>(event.mouse_pos.y)))
        {
            m_hovered_row = -1;
            return;
        }

        m_last_rect = rect;
        const Layout layout = build_layout(rect);
        m_hovered_row = -1;

        if (layout.results_rect.contains(static_cast<i32>(event.mouse_pos.x),
                                         static_cast<i32>(event.mouse_pos.y)))
        {
            const i32 local_row = (static_cast<i32>(event.mouse_pos.y) - static_cast<i32>(layout.results_rect.y)) /
                                  static_cast<i32>(kRowH);
            const i32 absolute = m_scroll_row + local_row;
            if (absolute >= 0 && absolute < static_cast<i32>(m_cached_rows.size()))
            {
                m_hovered_row = absolute;
            }
        }

        if (event.scroll_delta != 0.0f &&
            layout.results_rect.contains(static_cast<i32>(event.mouse_pos.x),
                                         static_cast<i32>(event.mouse_pos.y)))
        {
            i32 delta = static_cast<i32>(std::round(event.scroll_delta));
            if (delta == 0)
            {
                delta = event.scroll_delta > 0.0f ? 1 : -1;
            }

            m_scroll_row -= delta;
            const i32 max_scroll = std::max(0, static_cast<i32>(m_cached_rows.size()) - visible_row_count(rect));
            m_scroll_row = std::clamp(m_scroll_row, 0, max_scroll);
        }

        if (!event.was_click || event.click_button != shell::MouseButton::Left)
        {
            return;
        }

        m_search_focused = layout.search_box.contains(event.mouse_pos);

        for (i32 i = 0; i < 5; ++i)
        {
            if (!layout.filter_buttons[static_cast<std::size_t>(i)].contains(event.mouse_pos))
            {
                continue;
            }

            switch (i)
            {
                case 0: m_filters.stars = !m_filters.stars; break;
                case 1: m_filters.dsos = !m_filters.dsos; break;
                case 2: m_filters.solar = !m_filters.solar; break;
                case 3: m_filters.discovered = !m_filters.discovered; break;
                case 4: m_filters.candidates = !m_filters.candidates; break;
            }
            rebuild_cache(rect);
            return;
        }

        if (layout.level_button.contains(event.mouse_pos))
        {
            switch (m_min_level)
            {
                case MinLevel::L1: m_min_level = MinLevel::L2; break;
                case MinLevel::L2: m_min_level = MinLevel::L3; break;
                case MinLevel::L3: m_min_level = MinLevel::L4; break;
                case MinLevel::L4: m_min_level = MinLevel::L1; break;
            }
            rebuild_cache(rect);
            return;
        }

        if (layout.sort_button.contains(event.mouse_pos))
        {
            const auto next = static_cast<u8>((static_cast<u8>(m_sort_key) + 1u) % 4u);
            m_sort_key = static_cast<SortKey>(next);
            m_sort_dir = SortDir::Asc;
            rebuild_cache(rect);
            return;
        }

        if (m_hovered_row >= 0 && m_hovered_row < static_cast<i32>(m_cached_rows.size()))
        {
            const u64 object_id = m_cached_rows[static_cast<std::size_t>(m_hovered_row)].id;
            m_selected_object = object_id;
            sync_global_selection(object_id);
            rebuild_detail_cache();
            return;
        }

        if (!m_selected_object.has_value())
        {
            return;
        }

        if (layout.action_buttons[0].contains(event.mouse_pos))
        {
            if (m_hooks.open_observe_for)
            {
                m_hooks.open_observe_for(m_selected_object.value());
            }
            else
            {
                spdlog::info("TODO Sprint 10+: OBSERVE workflow");
            }
            return;
        }

        if (layout.action_buttons[1].contains(event.mouse_pos))
        {
            if (m_hooks.locate_in_planetarium)
            {
                m_hooks.locate_in_planetarium(m_selected_object.value());
            }
            return;
        }

        if (layout.action_buttons[2].contains(event.mouse_pos))
        {
            if (m_hooks.track_in_planetarium)
            {
                m_hooks.track_in_planetarium(m_selected_object.value());
            }
            return;
        }
    }

    shell::TabId EncyclopediaTab::get_id() const
    {
        return shell::TabId::Encyclopedia;
    }

    void EncyclopediaTab::rebuild_cache(const shell::ViewportRect& rect)
    {
        m_cached_rows.clear();

        const knowledge::KnowledgeLevel min_level = min_level_to_knowledge(m_min_level);
        const std::vector<u64> ids = m_knowledge.get_all_known_ids();
        m_cached_rows.reserve(ids.size());

        for (const u64 id : ids)
        {
            const auto object = m_universe.query_object(id);
            if (!object.has_value())
            {
                continue;
            }

            if (!passes_filters(*object))
            {
                continue;
            }

            knowledge::KnowledgeLevel level = object->is_real()
                ? knowledge::kHistoricalBaselineLevel
                : knowledge::KnowledgeLevel::Unknown;
            if (m_knowledge.is_known(object->id))
            {
                level = m_knowledge.get_level(object->id);
            }

            if (level < min_level)
            {
                continue;
            }

            CachedRow row{};
            row.id = object->id;
            row.designation = default_designation(*object, m_universe);
            row.subtitle = subtitle_for(*object);
            row.status_glyph = status_glyph(*object, m_knowledge);
            row.magnitude_key = object->mag_v;
            row.distance_key = 0.0;
            row.discovery_key = 0.0;

            if (const auto* star = std::get_if<universe::StarData>(&object->data))
            {
                row.distance_key = static_cast<f64>(star->distance_pc);
            }
            else if (const auto* solar = std::get_if<universe::SolarSystemData>(&object->data))
            {
                row.distance_key = static_cast<f64>(solar->distance_au);
            }
            else if (const auto* dso = std::get_if<universe::DsoData>(&object->data))
            {
                row.distance_key = static_cast<f64>(dso->size_arcmin);
            }

            const auto records = m_archive.get_by_target(object->id);
            if (!records.empty())
            {
                row.discovery_key = records.front()->observation_jd;
                for (const auto* record : records)
                {
                    row.discovery_key = std::min(row.discovery_key, record->observation_jd);
                }
            }

            if (!matches_search(row))
            {
                continue;
            }

            m_cached_rows.push_back(std::move(row));
        }

        auto compare = [this](const CachedRow& lhs, const CachedRow& rhs)
        {
            auto ord = [this](const auto& left, const auto& right)
            {
                if (left < right)
                {
                    return m_sort_dir == SortDir::Asc;
                }
                if (left > right)
                {
                    return m_sort_dir == SortDir::Desc;
                }
                return false;
            };

            switch (m_sort_key)
            {
                case SortKey::Name:
                    if (lhs.designation != rhs.designation)
                    {
                        return ord(lhs.designation, rhs.designation);
                    }
                    break;
                case SortKey::Distance:
                    if (lhs.distance_key != rhs.distance_key)
                    {
                        return ord(lhs.distance_key, rhs.distance_key);
                    }
                    break;
                case SortKey::Magnitude:
                    if (lhs.magnitude_key != rhs.magnitude_key)
                    {
                        return ord(lhs.magnitude_key, rhs.magnitude_key);
                    }
                    break;
                case SortKey::DiscoveryDate:
                    if (lhs.discovery_key != rhs.discovery_key)
                    {
                        return ord(lhs.discovery_key, rhs.discovery_key);
                    }
                    break;
            }

            return lhs.id < rhs.id;
        };

        std::sort(m_cached_rows.begin(), m_cached_rows.end(), compare);

        const i32 max_scroll = std::max(0, static_cast<i32>(m_cached_rows.size()) - visible_row_count(rect));
        m_scroll_row = std::clamp(m_scroll_row, 0, max_scroll);

        if (m_selected_object.has_value())
        {
            const auto selected_it = std::find_if(
                m_cached_rows.begin(),
                m_cached_rows.end(),
                [this](const CachedRow& row) { return row.id == m_selected_object.value(); });
            if (selected_it == m_cached_rows.end())
            {
                if (!m_universe.query_object(m_selected_object.value()).has_value())
                {
                    PLX_CORE_WARN("EncyclopediaTab: selected object {} no longer exists", m_selected_object.value());
                    m_selected_object.reset();
                }
            }
            else
            {
                ensure_selected_visible(rect);
            }
        }
    }

    void EncyclopediaTab::rebuild_detail_cache()
    {
        m_detail_cache = {};

        if (!m_selected_object.has_value())
        {
            return;
        }

        const auto object = m_universe.query_object(m_selected_object.value());
        if (!object.has_value())
        {
            return;
        }

        m_detail_cache.designation = default_designation(*object, m_universe);
        m_detail_cache.status_text = status_text(*object, m_knowledge);

        knowledge::KnowledgeLevel level = object->is_real()
            ? knowledge::kHistoricalBaselineLevel
            : knowledge::KnowledgeLevel::Unknown;
        if (m_knowledge.is_known(object->id))
        {
            level = m_knowledge.get_level(object->id);
        }

        m_detail_cache.status_level = knowledge_level_to_text(level);
        m_detail_cache.status_bg = status_badge_color(*object, m_knowledge);

        const auto& selected = m_selection.get_selection();
        const bool has_selected_match = selected.celestial_obj.id == object->id;
        const f64 alt = has_selected_match ? selected.alt_rad : 0.0;
        const f64 az = has_selected_match ? selected.az_rad : 0.0;

        m_detail_cache.position_rows.push_back({"RA", format_ra(object->ra), false});
        m_detail_cache.position_rows.push_back({"Dec", format_dec(object->dec), false});
        m_detail_cache.position_rows.push_back({"Alt", format_alt(alt), false});
        m_detail_cache.position_rows.push_back({"Az", format_az(az), false});

        const auto add_property = [&](std::vector<PropertyLine>& output,
                                      std::string label,
                                      std::string_view property_name,
                                      std::string measured_prefix = {})
        {
            const auto descriptor = knowledge::PropertyRegistry::get_property(object->type, property_name);
            if (!descriptor.has_value())
            {
                return;
            }

            const auto measured = m_knowledge.get_measurement(object->id, property_name);
            if (measured.has_value())
            {
                output.push_back({
                    std::move(label),
                    measured_prefix.empty()
                        ? std::format("{:.6g}", measured->value)
                        : std::format("{} {:.6g}", measured_prefix, measured->value),
                    false
                });
                return;
            }

            const bool is_unconfirmed = m_detail_cache.status_text == "UNCONFIRMED";
            if (object->is_real() && descriptor->unlocks_at <= knowledge::kHistoricalBaselineLevel)
            {
                if (const auto catalog = get_catalog_property_value(*object, property_name); catalog.has_value())
                {
                    output.push_back({std::move(label), std::format("{:.6g}", *catalog), false});
                }
                else
                {
                    output.push_back({std::move(label), "?", true});
                }
                return;
            }

            if (is_unconfirmed)
            {
                output.push_back({
                    std::move(label),
                    std::format("? (unlock via {})", measurement_technique_text(descriptor->required_technique)),
                    true
                });
                return;
            }

            if (descriptor->unlocks_at > level)
            {
                output.push_back({
                    std::move(label),
                    std::format("? (unlock via {})", measurement_technique_text(descriptor->required_technique)),
                    true
                });
                return;
            }

            output.push_back({std::move(label), "?", true});
        };

        add_property(m_detail_cache.photometry_rows, "V mag", "mag_v");
        add_property(m_detail_cache.photometry_rows, "B-V", "color_bv");

        if (object->type == universe::ObjectType::Star || object->type == universe::ObjectType::ProceduralStar)
        {
            add_property(m_detail_cache.type_rows, "Distance", "distance_pc");
            add_property(m_detail_cache.type_rows, "Parallax", "parallax_mas");
            add_property(m_detail_cache.type_rows, "Radial velocity", "radial_velocity_kms");
            add_property(m_detail_cache.type_rows, "Proper motion RA", "proper_motion_ra");
            add_property(m_detail_cache.type_rows, "Proper motion Dec", "proper_motion_dec");
            add_property(m_detail_cache.type_rows, "Spectral type", "spectral_type");
        }
        else if (object->type == universe::ObjectType::DeepSkyObject ||
                 object->type == universe::ObjectType::Galaxy ||
                 object->type == universe::ObjectType::ProceduralDso)
        {
            add_property(m_detail_cache.type_rows, "Type", "hubble_type");
            add_property(m_detail_cache.type_rows, "Angular size", "size_arcmin");
            add_property(m_detail_cache.type_rows, "Distance", "distance_mpc");
            add_property(m_detail_cache.type_rows, "Redshift", "redshift");
        }
        else if (object->type == universe::ObjectType::SolarSystemBody)
        {
            add_property(m_detail_cache.type_rows, "Phase", "phase_illumination");
            add_property(m_detail_cache.type_rows, "Distance (Earth)", "distance_au");
            add_property(m_detail_cache.type_rows, "Apparent diameter", "angular_diameter_arcsec");
            add_property(m_detail_cache.type_rows, "Magnitude", "mag_v");
        }

        m_detail_cache.discovery_rows.push_back({"Discovered:", m_detail_cache.status_text, false});
        const auto records = m_archive.get_by_target(object->id);
        m_detail_cache.discovery_rows.push_back({"Confirmations:", std::format("{} observations", records.size()), false});
        m_detail_cache.observation_count = static_cast<i32>(records.size());

        for (const auto& descriptor : knowledge::PropertyRegistry::get_properties(object->type))
        {
            if (descriptor.unlocks_at > knowledge::KnowledgeLevel::Detailed)
            {
                continue;
            }

            if (m_knowledge.get_measurement(object->id, descriptor.name).has_value())
            {
                continue;
            }

            if (descriptor.unlocks_at <= level)
            {
                continue;
            }

            m_detail_cache.discover_rows.push_back({
                descriptor.name,
                std::format("(unlock via {})", measurement_technique_text(descriptor.required_technique)),
                true
            });
        }

        if (m_detail_cache.discover_rows.empty())
        {
            m_detail_cache.discover_rows.push_back({"", "All properties at L4+", false});
        }
    }

    void EncyclopediaTab::sync_selection_from_global(const shell::ViewportRect& rect)
    {
        if (!m_selection.has_selection())
        {
            return;
        }

        const u64 selected_id = m_selection.get_selection().celestial_obj.id;
        if (selected_id == 0)
        {
            return;
        }

        if (!m_selected_object.has_value() || m_selected_object.value() != selected_id)
        {
            m_selected_object = selected_id;
            ensure_selected_visible(rect);
        }
    }

    void EncyclopediaTab::sync_global_selection(const u64 object_id)
    {
        const auto object = m_universe.query_object(object_id);
        if (!object.has_value())
        {
            return;
        }

        SelectedObject& selected = const_cast<SelectedObject&>(m_selection.get_selection());
        selected = {};
        selected.celestial_obj = *object;

        switch (object->type)
        {
            case universe::ObjectType::Star:
            case universe::ObjectType::ProceduralStar:
            {
                selected.type = SelectedObjectType::Star;
                selected.is_procedural = object->type == universe::ObjectType::ProceduralStar;
                if (const auto* star = std::get_if<universe::StarData>(&object->data))
                {
                    selected.hip_id = star->hip_id;
                }
                selected.designation = default_designation(*object, m_universe);
                break;
            }
            case universe::ObjectType::DeepSkyObject:
            case universe::ObjectType::Galaxy:
            case universe::ObjectType::ProceduralDso:
            {
                selected.type = SelectedObjectType::Dso;
                selected.designation = default_designation(*object, m_universe);
                if (const auto* dso = std::get_if<universe::DsoData>(&object->data))
                {
                    selected.size_arcmin = dso->size_arcmin;
                    selected.dso_type = dso->dso_type;
                }
                break;
            }
            case universe::ObjectType::SolarSystemBody:
            {
                selected.type = SelectedObjectType::SolarSystem;
                selected.body_id = static_cast<u32>(universe::decode_source_id(object->id));
                selected.body_name = default_designation(*object, m_universe);
                if (const auto* solar = std::get_if<universe::SolarSystemData>(&object->data))
                {
                    selected.distance_au = solar->distance_au;
                    selected.angular_diameter_arcsec = solar->apparent_diameter_arcsec;
                    selected.phase_angle_deg = solar->phase_angle;
                    selected.illumination = solar->illumination;
                }
                break;
            }
            default:
                selected.type = SelectedObjectType::None;
                break;
        }

        selected.ra_rad = object->ra;
        selected.dec_rad = object->dec;
        selected.mag_v = object->mag_v;
        selected.color_bv = object->color_bv;
    }

    void EncyclopediaTab::ensure_selected_visible(const shell::ViewportRect& rect)
    {
        if (!m_selected_object.has_value())
        {
            return;
        }

        const auto it = std::find_if(
            m_cached_rows.begin(),
            m_cached_rows.end(),
            [this](const CachedRow& row) { return row.id == m_selected_object.value(); });

        if (it == m_cached_rows.end())
        {
            return;
        }

        const i32 row_index = static_cast<i32>(std::distance(m_cached_rows.begin(), it));
        const i32 visible = std::max(1, visible_row_count(rect));

        if (row_index < m_scroll_row)
        {
            m_scroll_row = row_index;
        }
        else if (!row_in_view(row_index, visible))
        {
            m_scroll_row = std::max(0, row_index - visible + 1);
        }
    }

    bool EncyclopediaTab::passes_filters(const universe::CelestialObject& object) const
    {
        const bool is_star = object.type == universe::ObjectType::Star ||
                             object.type == universe::ObjectType::ProceduralStar;
        const bool is_dso = object.type == universe::ObjectType::DeepSkyObject ||
                            object.type == universe::ObjectType::Galaxy ||
                            object.type == universe::ObjectType::ProceduralDso;
        const bool is_solar = object.type == universe::ObjectType::SolarSystemBody;

        if ((is_star && !m_filters.stars) ||
            (is_dso && !m_filters.dsos) ||
            (is_solar && !m_filters.solar))
        {
            return false;
        }

        if (!object.is_real())
        {
            if (m_knowledge.is_confirmed(object.id))
            {
                if (!m_filters.discovered)
                {
                    return false;
                }
            }
            else if (!m_filters.candidates)
            {
                return false;
            }
        }

        return true;
    }

    bool EncyclopediaTab::matches_search(const CachedRow& row) const
    {
        if (m_search_query.empty())
        {
            return true;
        }

        auto lower = [](std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        };

        const std::string query = lower(m_search_query);
        return lower(row.designation).find(query) != std::string::npos ||
               lower(row.subtitle).find(query) != std::string::npos;
    }

    bool EncyclopediaTab::row_in_view(const i32 absolute_row, const i32 visible_rows) const
    {
        return absolute_row >= m_scroll_row && absolute_row < (m_scroll_row + visible_rows);
    }

    i32 EncyclopediaTab::visible_row_count(const shell::ViewportRect& rect) const
    {
        if (!rect.is_valid())
        {
            return 1;
        }

        const Layout layout = build_layout(rect);
        return std::max(1, static_cast<i32>(layout.results_rect.height / kRowH));
    }

    std::string EncyclopediaTab::format_ra(f64 ra_rad)
    {
        using namespace parallax::astro_constants;

        const f64 hours = ra_rad * kRadToHour;
        const f64 abs_h = std::abs(hours);
        const i32 h = static_cast<i32>(abs_h);
        const f64 min_f = (abs_h - h) * 60.0;
        const i32 m = static_cast<i32>(min_f);
        const f64 s = (min_f - m) * 60.0;

        char buf[32];
        std::snprintf(buf, sizeof(buf), "%02dh %02dm %04.1fs", h, m, s);
        return buf;
    }

    std::string EncyclopediaTab::format_dec(f64 dec_rad)
    {
        using namespace parallax::astro_constants;

        const f64 deg = dec_rad * kRadToDeg;
        const char sign = deg >= 0.0 ? '+' : '-';
        const f64 abs_deg = std::abs(deg);
        const i32 d = static_cast<i32>(abs_deg);
        const f64 min_f = (abs_deg - d) * 60.0;
        const i32 m = static_cast<i32>(min_f);
        const i32 s = static_cast<i32>((min_f - m) * 60.0);

        char buf[32];
        std::snprintf(buf, sizeof(buf), "%c%02d %02d' %02d\"", sign, d, m, s);
        return buf;
    }

    std::string EncyclopediaTab::format_alt(f64 alt_rad)
    {
        using namespace parallax::astro_constants;

        const f64 deg = alt_rad * kRadToDeg;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%+.2f deg", deg);
        return buf;
    }

    std::string EncyclopediaTab::format_az(f64 az_rad)
    {
        using namespace parallax::astro_constants;

        f64 deg = az_rad * kRadToDeg;
        if (deg < 0.0)
        {
            deg += 360.0;
        }

        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.2f deg", deg);
        return buf;
    }
}

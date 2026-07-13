#include "ui/shell/shell.hpp"

#include "astro/coordinates.hpp"
#include "astro/observer.hpp"
#include "astro/time_system.hpp"
#include "core/input.hpp"
#include "core/logger.hpp"
#include "core/user_data_path.hpp"
#include "knowledge/knowledge_database.hpp"
#include "instruments/array_instrument.hpp"
#include "observation/data_archive.hpp"
#include "observation/session_scheduler.hpp"
#include "rendering/camera.hpp"
#include "rendering/line_renderer.hpp"
#include "ui/font.hpp"
#include "ui/panel_system.hpp"
#include "ui/selection.hpp"
#include "ui/tabs/allsky_tab.hpp"
#include "ui/tabs/analysis_tab.hpp"
#include "ui/tabs/archive_tab.hpp"
#include "ui/tabs/base_tab.hpp"
#include "ui/tabs/encyclopedia_tab.hpp"
#include "ui/tabs/imaging_tab.hpp"
#include "ui/tabs/planetarium_tab.hpp"
#include "ui/tabs/spectroscopy_tab.hpp"
#include "universe/universe.hpp"
#include "vulkan/context.hpp"
#include "vulkan/swapchain.hpp"

#include <SDL2/SDL_scancode.h>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <fstream>
#include <optional>
#include <ranges>
#include <string_view>

namespace parallax::ui::shell
{
    namespace
    {
        using json = nlohmann::json;

        [[nodiscard]] constexpr PaneKind split_kind_for_axis(const SplitterAxis axis) noexcept
        {
            return axis == SplitterAxis::Vertical ? PaneKind::HorizontalSplit : PaneKind::VerticalSplit;
        }

        [[nodiscard]] std::optional<TabId> tab_from_persistence_key(const std::string_view key)
        {
            for (u32 i = 0; i < kTabIdCount; ++i)
            {
                const TabId candidate = static_cast<TabId>(i);
                if (key == to_persistence_key(candidate))
                {
                    return candidate;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] json serialize_pane(const Pane* pane)
        {
            if (pane == nullptr)
            {
                return json::object();
            }

            if (pane->is_leaf())
            {
                json tabs = json::array();
                for (const TabId tab : pane->get_tabs())
                {
                    tabs.push_back(to_persistence_key(tab));
                }

                const TabId active = pane->get_tabs().empty() ? TabId::Planetarium : pane->get_active_tab();

                return json{
                    {"type", "leaf"},
                    {"tabs", std::move(tabs)},
                    {"active_tab", to_persistence_key(active)}
                };
            }

            return json{
                {"type", pane->get_kind() == PaneKind::HorizontalSplit ? "horizontal_split" : "vertical_split"},
                {"ratio", pane->get_split_ratio()},
                {"first", serialize_pane(pane->get_first())},
                {"second", serialize_pane(pane->get_second())}
            };
        }

        [[nodiscard]] std::optional<TabId> first_tab_in_layout(const json& node)
        {
            if (!node.is_object())
            {
                return std::nullopt;
            }

            const std::string type = node.value("type", "");
            if (type == "leaf")
            {
                if (!node.contains("tabs") || !node["tabs"].is_array() || node["tabs"].empty())
                {
                    return std::nullopt;
                }

                for (const json& tab_json : node["tabs"])
                {
                    if (!tab_json.is_string())
                    {
                        continue;
                    }

                    if (const std::optional<TabId> tab = tab_from_persistence_key(tab_json.get<std::string>()); tab.has_value())
                    {
                        return tab;
                    }
                }

                return std::nullopt;
            }

            if (node.contains("first"))
            {
                if (const std::optional<TabId> first = first_tab_in_layout(node["first"]); first.has_value())
                {
                    return first;
                }
            }
            if (node.contains("second"))
            {
                return first_tab_in_layout(node["second"]);
            }
            return std::nullopt;
        }

        [[nodiscard]] bool apply_layout_to_pane(Pane* pane, const json& node)
        {
            if (pane == nullptr || !node.is_object())
            {
                return false;
            }

            const std::string type = node.value("type", "");

            if (type == "leaf")
            {
                if (!node.contains("tabs") || !node["tabs"].is_array() || node["tabs"].empty())
                {
                    return false;
                }

                if (!pane->is_leaf())
                {
                    return false;
                }

                std::vector<TabId> desired_tabs;
                desired_tabs.reserve(node["tabs"].size());
                for (const json& tab_json : node["tabs"])
                {
                    if (!tab_json.is_string())
                    {
                        continue;
                    }

                    const std::optional<TabId> tab = tab_from_persistence_key(tab_json.get<std::string>());
                    if (tab.has_value() && std::ranges::find(desired_tabs, tab.value()) == desired_tabs.end())
                    {
                        desired_tabs.push_back(tab.value());
                    }
                }

                if (desired_tabs.empty())
                {
                    return false;
                }

                std::vector<TabId> existing_tabs;
                for (const TabId tab : pane->get_tabs())
                {
                    existing_tabs.push_back(tab);
                }
                for (const TabId tab : existing_tabs)
                {
                    pane->remove_tab(tab);
                }

                for (const TabId tab : desired_tabs)
                {
                    pane->add_tab(tab);
                }

                TabId active_tab = desired_tabs.front();
                if (node.contains("active_tab") && node["active_tab"].is_string())
                {
                    const std::optional<TabId> loaded_active = tab_from_persistence_key(node["active_tab"].get<std::string>());
                    if (loaded_active.has_value() && std::ranges::find(desired_tabs, loaded_active.value()) != desired_tabs.end())
                    {
                        active_tab = loaded_active.value();
                    }
                }
                pane->set_active_tab(active_tab);
                return true;
            }

            if (type != "horizontal_split" && type != "vertical_split")
            {
                return false;
            }

            if (!node.contains("first") || !node.contains("second"))
            {
                return false;
            }

            const std::optional<TabId> second_seed = first_tab_in_layout(node["second"]);
            if (!second_seed.has_value())
            {
                return false;
            }

            if (!pane->is_leaf())
            {
                return false;
            }

            pane->split(type == "horizontal_split" ? PaneKind::HorizontalSplit : PaneKind::VerticalSplit,
                        second_seed.value(),
                        false);

            if (node.contains("ratio") && node["ratio"].is_number())
            {
                pane->set_split_ratio(std::clamp(node["ratio"].get<f32>(), kMinSplitRatio, kMaxSplitRatio));
            }

            return apply_layout_to_pane(pane->get_first(), node["first"])
                && apply_layout_to_pane(pane->get_second(), node["second"]);
        }

        struct ChildRects
        {
            ViewportRect first{};
            ViewportRect second{};
        };

        [[nodiscard]] ChildRects compute_child_rects(const Pane* pane, const ViewportRect& available)
        {
            ChildRects rects{};
            const i32 start_x = static_cast<i32>(available.x);
            const i32 start_y = static_cast<i32>(available.y);
            const i32 end_x = static_cast<i32>(available.right());
            const i32 end_y = static_cast<i32>(available.bottom());
            const i32 half = static_cast<i32>(kSplitterThickness / 2);
            const i32 other_half = static_cast<i32>(kSplitterThickness - (kSplitterThickness / 2));

            if (pane->get_kind() == PaneKind::HorizontalSplit)
            {
                const i32 split_x = start_x + static_cast<i32>(std::lround(static_cast<f32>(available.width) * pane->get_split_ratio()));
                const i32 first_end_x = split_x - half;
                const i32 second_start_x = split_x + other_half;

                rects.first = {
                    static_cast<u32>(start_x),
                    static_cast<u32>(start_y),
                    static_cast<u32>(std::max(0, first_end_x - start_x)),
                    available.height
                };
                rects.second = {
                    static_cast<u32>(std::max(second_start_x, start_x)),
                    static_cast<u32>(start_y),
                    static_cast<u32>(std::max(0, end_x - second_start_x)),
                    available.height
                };
                return rects;
            }

            const i32 split_y = start_y + static_cast<i32>(std::lround(static_cast<f32>(available.height) * pane->get_split_ratio()));
            const i32 first_end_y = split_y - half;
            const i32 second_start_y = split_y + other_half;

            rects.first = {
                static_cast<u32>(start_x),
                static_cast<u32>(start_y),
                available.width,
                static_cast<u32>(std::max(0, first_end_y - start_y))
            };
            rects.second = {
                static_cast<u32>(start_x),
                static_cast<u32>(std::max(second_start_y, start_y)),
                available.width,
                static_cast<u32>(std::max(0, end_y - second_start_y))
            };
            return rects;
        }

        [[nodiscard]] bool find_pane_rect(const Pane* node,
                                          const Pane* target,
                                          const ViewportRect& current_rect,
                                          ViewportRect& out_rect)
        {
            if (node == nullptr)
            {
                return false;
            }

            if (node == target)
            {
                out_rect = current_rect;
                return true;
            }

            if (node->is_leaf())
            {
                return false;
            }

            const ChildRects child_rects = compute_child_rects(node, current_rect);
            return find_pane_rect(node->get_first(), target, child_rects.first, out_rect)
                || find_pane_rect(node->get_second(), target, child_rects.second, out_rect);
        }

        [[nodiscard]] Vec2f pixel_to_ndc(const Vec2f px, const Vec2f viewport)
        {
            return {
                (px.x / viewport.x) * 2.0f - 1.0f,
                (px.y / viewport.y) * 2.0f - 1.0f
            };
        }

        void draw_filled_rect(rendering::LineRenderer& lines,
                              const ViewportRect& rect,
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
                lines.add_line(pixel_to_ndc({x0, py}, viewport), pixel_to_ndc({x1, py}, viewport), color);
            }
        }

        void draw_splitters_recursive(rendering::LineRenderer& lines,
                                      const Pane* pane,
                                      const ViewportRect& rect,
                                      const Vec2f viewport)
        {
            if (pane == nullptr || pane->is_leaf())
            {
                return;
            }

            draw_filled_rect(lines, pane->get_splitter_rect(rect), Vec4f{0.0f, 0.55f, 0.0f, 1.0f}, viewport);
            const ChildRects child_rects = compute_child_rects(pane, rect);
            draw_splitters_recursive(lines, pane->get_first(), child_rects.first, viewport);
            draw_splitters_recursive(lines, pane->get_second(), child_rects.second, viewport);
        }
    }

    Shell::Shell(BitmapFont& font,
                 rendering::LineRenderer& line_renderer,
                 PanelSystem& panel_system,
                 const vulkan::Context& context,
                 vulkan::Swapchain& swapchain,
                 const VkRenderPass render_pass,
                 const std::filesystem::path& shader_dir,
                 universe::Universe& universe,
                 instruments::ArrayInstrument& array_instrument,
                 knowledge::KnowledgeDatabase& knowledge,
                 observation::DataArchive& archive,
                 observation::SessionScheduler& scheduler,
                 astro::ObserverRegistry& observer_registry,
                 Selection& selection,
                 core::Input& input,
                 f64& julian_date,
                 f64& time_scale)
        : m_font(font)
        , m_line_renderer(line_renderer)
        , m_panel_system(panel_system)
        , m_context(context)
        , m_swapchain(swapchain)
        , m_render_pass(render_pass)
        , m_shader_dir(shader_dir)
        , m_universe(universe)
        , m_array_instrument(array_instrument)
        , m_knowledge(knowledge)
        , m_archive(archive)
        , m_scheduler(scheduler)
        , m_observer_registry(observer_registry)
        , m_selection(selection)
        , m_input(input)
        , m_julian_date(julian_date)
        , m_time_scale(time_scale)
    {
        m_planetarium = std::make_unique<tabs::PlanetariumTab>(m_context,
                                                                m_swapchain,
                                                                m_render_pass,
                                                                m_shader_dir,
                                                                m_universe,
                                                                m_knowledge,
                                                                m_font,
                                                                m_julian_date,
                                                                m_observer_registry);
        m_imaging = std::make_unique<tabs::ImagingTab>(m_context,
                                                       m_swapchain,
                                                       m_render_pass,
                                                       m_shader_dir,
                                                       m_universe,
                                                       m_array_instrument,
                                                       m_scheduler,
                                                       m_selection,
                                                       m_font,
                                                       m_julian_date);
        m_spectroscopy = std::make_unique<tabs::SpectroscopyTab>(m_font);
        m_analysis = std::make_unique<tabs::AnalysisTab>(m_font);
        m_archive_tab = std::make_unique<tabs::ArchiveTab>(m_font, m_line_renderer, m_swapchain, m_archive);
        m_encyclopedia = std::make_unique<tabs::EncyclopediaTab>(m_font,
                                                                  m_line_renderer,
                                                                  m_swapchain,
                                                                  m_universe,
                                                                  m_knowledge,
                                                                  m_archive,
                                                                  m_selection);
        m_allsky = std::make_unique<tabs::AllskyTab>(m_font);
        m_base = std::make_unique<tabs::BaseTab>(m_font, m_observer_registry, m_archive);

        m_tabs_by_id.fill(nullptr);
        m_tabs_by_id[static_cast<u32>(TabId::Planetarium)] = m_planetarium.get();
        m_tabs_by_id[static_cast<u32>(TabId::Imaging)] = m_imaging.get();
        m_tabs_by_id[static_cast<u32>(TabId::Spectroscopy)] = m_spectroscopy.get();
        m_tabs_by_id[static_cast<u32>(TabId::Analysis)] = m_analysis.get();
        m_tabs_by_id[static_cast<u32>(TabId::Archive)] = m_archive_tab.get();
        m_tabs_by_id[static_cast<u32>(TabId::Encyclopedia)] = m_encyclopedia.get();
        m_tabs_by_id[static_cast<u32>(TabId::AllSky)] = m_allsky.get();
        m_tabs_by_id[static_cast<u32>(TabId::Base)] = m_base.get();

        m_last_archive_size = m_archive.size();
    }

    Shell::~Shell() = default;

    void Shell::init()
    {
        m_sidebar = std::make_unique<Sidebar>(m_font);
        m_top_bar = std::make_unique<TopBar>(m_font);
        m_status_bar = std::make_unique<StatusBar>(m_font);

        bind_callbacks();
        load_layout_or_default();
        m_last_archive_size = m_archive.size();
    }

    void Shell::set_callbacks(ShellCallbacks callbacks)
    {
        m_callbacks = std::move(callbacks);
        bind_callbacks();
    }

    void Shell::bind_callbacks()
    {
        if (!m_sidebar)
        {
            return;
        }

        SidebarCallbacks sidebar_callbacks;
        sidebar_callbacks.on_tab_open = [this](const TabId id, const TabOpenMode mode)
        {
            switch (mode)
            {
                case TabOpenMode::ReplaceFocused:
                    open_tab(id);
                    break;
                case TabOpenMode::SplitRight:
                    open_tab_split(id, SplitterAxis::Vertical, true);
                    break;
                case TabOpenMode::SplitBottom:
                    open_tab_split(id, SplitterAxis::Horizontal, true);
                    break;
            }
        };

        sidebar_callbacks.on_instrument_selected = [this](std::string_view id)
        {
            focus_tab(TabId::Base);
            PLX_CORE_INFO("Instrument selected in shell sidebar: {}", id);
        };

        sidebar_callbacks.on_pause_toggle = [this]()
        {
            if (m_callbacks.on_pause_toggle)
            {
                m_callbacks.on_pause_toggle();
            }
        };
        sidebar_callbacks.on_time_scale_set = [this](const f64 scale)
        {
            if (m_callbacks.on_time_scale_set)
            {
                m_callbacks.on_time_scale_set(scale);
            }
        };
        sidebar_callbacks.on_time_scale_up = [this]()
        {
            if (m_callbacks.on_time_scale_up)
            {
                m_callbacks.on_time_scale_up();
            }
        };
        sidebar_callbacks.on_time_scale_down = [this]()
        {
            if (m_callbacks.on_time_scale_down)
            {
                m_callbacks.on_time_scale_down();
            }
        };
        m_sidebar->set_callbacks(std::move(sidebar_callbacks));

        m_encyclopedia->set_shell_hooks({
            .locate_in_planetarium = [this](const u64 id)
            {
                focus_tab(TabId::Planetarium);
                m_planetarium->center_on(id);
            },
            .track_in_planetarium = [this](const u64 id)
            {
                focus_tab(TabId::Planetarium);
                m_planetarium->start_tracking(id);
            },
            .open_observe_for = [this](const u64 id)
            {
                m_imaging->set_target(id);
                focus_tab(TabId::Imaging);
            }
        });

        m_base->set_shell_hooks({
            .on_instrument_selected = [this](std::string instrument_id)
            {
                focus_tab(TabId::Planetarium);
                PLX_CORE_INFO("Base tab selected instrument '{}' (routing lands in Sprint 10+).", instrument_id);
            }
        });
    }

    void Shell::update(f64 delta_time)
    {
        for (TabContent* tab : m_tabs_by_id)
        {
            if (tab != nullptr)
            {
                tab->update(delta_time);
            }
        }

        if (m_status_bar)
        {
            m_status_bar->tick(static_cast<f32>(delta_time));
        }

        const std::size_t archive_size = m_archive.size();
        if (archive_size < m_last_archive_size)
        {
            push_notification("Archive entry deleted", NotificationSeverity::Info);
        }
        m_last_archive_size = archive_size;

        update_chrome_state(delta_time);
    }

    void Shell::update_chrome_state(f64 delta_time)
    {
        if (!m_sidebar || !m_top_bar || !m_status_bar || !m_pane_tree)
        {
            return;
        }

        std::array<bool, kTabIdCount> visible_tabs{};
        for (const auto& [pane, rect] : m_pane_tree->get_leaves())
        {
            if (pane == nullptr || !rect.is_valid() || pane->is_empty_leaf())
            {
                continue;
            }
            visible_tabs[static_cast<u32>(pane->get_active_tab())] = true;
        }

        SidebarState sidebar_state;
        {
            const u32 active_stations = m_array_instrument.get_active_station_count();
            const bool sessions_active = !m_scheduler.get_active().empty();
            sidebar_state.instruments.push_back({
                .id     = "glasswing_array",
                .name   = m_array_instrument.get_name(),
                .status = sessions_active
                    ? fmt::format("integrating ({} sta.)", active_stations)
                    : fmt::format("idle ({} sta.)", active_stations)
            });
            sidebar_state.selected_instrument_id = "glasswing_array";
        }
        sidebar_state.visible_tabs = visible_tabs;
        sidebar_state.time.paused = (m_time_scale == 0.0);
        sidebar_state.time.time_scale = m_time_scale;
        sidebar_state.stats.push_back({
            .label = "Observations",
            .value = fmt::format("{}", m_archive.size())
        });
        m_sidebar->set_state(std::move(sidebar_state));

        const astro::ObserverLocation& active = m_observer_registry.get_active();
        const astro::DateTime dt = astro::TimeSystem::from_julian_date(m_julian_date);
        m_top_bar->set_state({
            .location_name = active.name,
            .julian_date = m_julian_date,
            .civil_time = fmt::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02.0f}",
                                      dt.year,
                                      dt.month,
                                      dt.day,
                                      dt.hour,
                                      dt.minute,
                                      dt.second),
            .atmosphere_on = m_planetarium->atmosphere_effectively_on(),
            .vacuum_site = (active.parent_body != astro::ParentBody::Earth),
            .bortle_scale = active.bortle_scale
        });

        const auto active_sessions = m_scheduler.get_active();
        m_status_bar->set_state({
            .active_session_count = static_cast<u32>(active_sessions.size()),
            .any_session_running = !active_sessions.empty(),
            .time_scale = m_time_scale,
            .time_paused = (m_time_scale == 0.0),
            .fps = delta_time > 0.0 ? static_cast<f32>(1.0 / delta_time) : 0.0f
        });
    }

    ViewportRect Shell::compute_center_area(const ViewportRect& window) const
    {
        const ViewportRect top_bar_rect = TopBar::compute_rect(window);
        const ViewportRect status_bar_rect = StatusBar::compute_rect(window);

        const u32 remaining_y = top_bar_rect.bottom();
        const u32 remaining_bottom = status_bar_rect.y;
        const u32 remaining_h = remaining_bottom > remaining_y ? remaining_bottom - remaining_y : 0;

        const ViewportRect remaining{
            .x = window.x,
            .y = remaining_y,
            .width = window.width,
            .height = remaining_h
        };
        const ViewportRect sidebar_rect = Sidebar::compute_rect(remaining);

        const u32 center_x = sidebar_rect.right();
        const u32 center_w = remaining.right() > center_x ? remaining.right() - center_x : 0;

        return {
            .x = center_x,
            .y = remaining.y,
            .width = center_w,
            .height = remaining.height
        };
    }

    void Shell::render(VkCommandBuffer cmd, const ViewportRect& window_rect)
    {
        if (!m_sidebar || !m_top_bar || !m_status_bar || !m_pane_tree)
        {
            return;
        }

        const VkExtent2D extent = m_swapchain.get_extent();
        const ViewportRect top_bar_rect = TopBar::compute_rect(window_rect);
        const ViewportRect status_bar_rect = StatusBar::compute_rect(window_rect);
        const ViewportRect remaining{
            .x = window_rect.x,
            .y = top_bar_rect.bottom(),
            .width = window_rect.width,
            .height = status_bar_rect.y > top_bar_rect.bottom() ? status_bar_rect.y - top_bar_rect.bottom() : 0
        };
        const ViewportRect sidebar_rect = Sidebar::compute_rect(remaining);
        const ViewportRect center_area = compute_center_area(window_rect);

        m_pane_tree->update_layout(center_area);
        for (const auto& [pane, rect] : m_pane_tree->get_leaves())
        {
            if (pane == nullptr || pane->is_empty_leaf())
            {
                continue;
            }

            tab_for(pane->get_active_tab()).render(cmd, rect);
        }

        // Reset viewport+scissor to the full window BEFORE chrome draw calls
        // fire. The last tab to render left the Vulkan viewport pointing at its
        // pane rect; if we do not reset here, the sidebar/topbar/statusbar
        // geometry (authored in window-pixel space, NDC-mapped against the full
        // window) gets squashed into that pane rect, and the on-screen positions
        // no longer match the hit-rects used by handle_input.
        VkViewport full_viewport{};
        full_viewport.x = static_cast<f32>(window_rect.x);
        full_viewport.y = static_cast<f32>(window_rect.y);
        full_viewport.width = static_cast<f32>(window_rect.width);
        full_viewport.height = static_cast<f32>(window_rect.height);
        full_viewport.minDepth = 0.0f;
        full_viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &full_viewport);

        VkRect2D full_scissor{};
        full_scissor.offset = {static_cast<i32>(window_rect.x), static_cast<i32>(window_rect.y)};
        full_scissor.extent = {window_rect.width, window_rect.height};
        vkCmdSetScissor(cmd, 0, 1, &full_scissor);

        m_line_renderer.begin_frame();
        draw_splitters_recursive(m_line_renderer,
                                 m_pane_tree->get_root(),
                                 center_area,
                                 Vec2f{static_cast<f32>(extent.width), static_cast<f32>(extent.height)});
        m_sidebar->render(m_line_renderer, m_panel_system, cmd, extent, sidebar_rect);
        m_top_bar->render(m_line_renderer, m_panel_system, cmd, extent, top_bar_rect);
        m_status_bar->render(m_line_renderer, m_panel_system, cmd, extent, status_bar_rect);
        m_line_renderer.render(cmd);
        m_font.render(cmd, extent);
    }

    InputEvent Shell::make_local_event(const InputEvent& screen_event, const ViewportRect& rect) const
    {
        InputEvent local = screen_event;
        local.inside_viewport = rect.contains(screen_event.mouse_pos);
        if (local.inside_viewport)
        {
            local.mouse_pos = {
                screen_event.mouse_pos.x - static_cast<f32>(rect.x),
                screen_event.mouse_pos.y - static_cast<f32>(rect.y)
            };
        }

        if (screen_event.was_click && rect.contains(screen_event.click_pos))
        {
            local.was_click = true;
            local.click_pos = {
                screen_event.click_pos.x - static_cast<f32>(rect.x),
                screen_event.click_pos.y - static_cast<f32>(rect.y)
            };
        }
        else
        {
            local.was_click = false;
            local.click_button = MouseButton::None;
        }

        return local;
    }

    void Shell::apply_global_shortcuts()
    {
        if (m_input.is_key_pressed(SDL_SCANCODE_SPACE) && m_callbacks.on_pause_toggle)
        {
            m_callbacks.on_pause_toggle();
        }
        if ((m_input.is_key_pressed(SDL_SCANCODE_MINUS) || m_input.is_key_pressed(SDL_SCANCODE_KP_MINUS))
            && m_callbacks.on_time_scale_down)
        {
            m_callbacks.on_time_scale_down();
        }
        if (m_input.is_key_pressed(SDL_SCANCODE_KP_PLUS) && m_callbacks.on_time_scale_up)
        {
            m_callbacks.on_time_scale_up();
        }
        if (m_input.is_key_pressed(SDL_SCANCODE_EQUALS) && m_callbacks.on_time_reset)
        {
            m_callbacks.on_time_reset();
        }
    }

    void Shell::apply_planetarium_shortcuts()
    {
        if (m_input.is_key_pressed(SDL_SCANCODE_C))
        {
            m_planetarium->toggle_constellations();
        }
        if (m_input.is_key_pressed(SDL_SCANCODE_S))
        {
            m_planetarium->toggle_solar_system();
        }
        if (m_input.is_key_pressed(SDL_SCANCODE_D))
        {
            m_planetarium->toggle_dso();
        }
        if (m_input.is_key_pressed(SDL_SCANCODE_G))
        {
            m_planetarium->cycle_grid();
        }
        if (m_input.is_key_pressed(SDL_SCANCODE_A))
        {
            m_planetarium->toggle_atmosphere();
        }
        if (m_input.is_key_pressed(SDL_SCANCODE_H) || m_input.is_key_pressed(SDL_SCANCODE_O))
        {
            m_planetarium->toggle_horizon();
        }
    }

    bool Shell::handle_splitter_drag(const InputEvent& event, const ViewportRect& center_area)
    {
        if (!m_pane_tree || !m_pane_tree->get_root())
        {
            return false;
        }

        if (!event.is_dragging)
        {
            if (!m_input.is_left_button_down())
            {
                m_dragging_splitter = nullptr;
            }
            return false;
        }

        if (m_dragging_splitter == nullptr)
        {
            const std::optional<SplitterHit> hit = m_pane_tree->find_splitter_at(event.mouse_pos, center_area);
            if (!hit.has_value())
            {
                return false;
            }
            m_dragging_splitter = hit->pane;
        }

        ViewportRect pane_rect{};
        if (!find_pane_rect(m_pane_tree->get_root(), m_dragging_splitter, center_area, pane_rect))
        {
            return false;
        }

        if (m_dragging_splitter->get_kind() == PaneKind::HorizontalSplit)
        {
            const f32 span = static_cast<f32>(std::max(pane_rect.width, 1u));
            m_dragging_splitter->set_split_ratio(m_dragging_splitter->get_split_ratio() + (event.drag_delta.x / span));
        }
        else
        {
            const f32 span = static_cast<f32>(std::max(pane_rect.height, 1u));
            m_dragging_splitter->set_split_ratio(m_dragging_splitter->get_split_ratio() + (event.drag_delta.y / span));
        }

        m_pane_tree->update_layout(center_area);
        return true;
    }

    bool Shell::on_input(const InputEvent& event)
    {
        if (!m_sidebar || !m_top_bar || !m_status_bar || !m_pane_tree)
        {
            return false;
        }

        apply_global_shortcuts();
        apply_planetarium_shortcuts();

        const ViewportRect window{
            .x = 0,
            .y = 0,
            .width = m_swapchain.get_extent().width,
            .height = m_swapchain.get_extent().height
        };

        const ViewportRect top_bar_rect = TopBar::compute_rect(window);
        const ViewportRect status_bar_rect = StatusBar::compute_rect(window);
        const ViewportRect remaining{
            .x = window.x,
            .y = top_bar_rect.bottom(),
            .width = window.width,
            .height = status_bar_rect.y > top_bar_rect.bottom() ? status_bar_rect.y - top_bar_rect.bottom() : 0
        };
        const ViewportRect sidebar_rect = Sidebar::compute_rect(remaining);
        const ViewportRect center_area = compute_center_area(window);

        m_pane_tree->update_layout(center_area);

        if (m_sidebar->handle_input(event, sidebar_rect))
        {
            return true;
        }
        if (m_top_bar->handle_input(event, top_bar_rect))
        {
            return true;
        }
        if (m_status_bar->handle_input(event, status_bar_rect))
        {
            return true;
        }
        if (handle_splitter_drag(event, center_area))
        {
            return true;
        }

        // Routing policy: chrome first, then splitter drag, then pane-under-cursor (or focused pane fallback) tab input.
        Pane* target_pane = m_pane_tree->find_leaf_at(event.mouse_pos, center_area);
        ViewportRect target_rect{};
        for (const auto& [pane, rect] : m_pane_tree->get_leaves())
        {
            if (pane == target_pane)
            {
                target_rect = rect;
                break;
            }
        }

        if (target_pane == nullptr)
        {
            target_pane = m_focused_pane;
            for (const auto& [pane, rect] : m_pane_tree->get_leaves())
            {
                if (pane == target_pane)
                {
                    target_rect = rect;
                    break;
                }
            }
        }

        if (target_pane == nullptr || target_pane->is_empty_leaf())
        {
            return false;
        }

        if (event.was_click && event.click_button == MouseButton::Left && target_pane != m_focused_pane)
        {
            focus_pane(target_pane);
        }

        const InputEvent local_event = make_local_event(event, target_rect);
        tab_for(target_pane->get_active_tab()).on_input(local_event, target_rect);

        // TODO(Sprint 10): right-click split context menu remains deferred until richer mouse press context exists.
        return local_event.inside_viewport || local_event.was_click || local_event.scroll_delta != 0.0f || local_event.is_dragging;
    }

    void Shell::open_tab(const TabId id)
    {
        if (!m_pane_tree)
        {
            return;
        }

        if (m_focused_pane == nullptr)
        {
            m_pane_tree->update_layout(compute_center_area({
                .x = 0,
                .y = 0,
                .width = m_swapchain.get_extent().width,
                .height = m_swapchain.get_extent().height
            }));
            if (!m_pane_tree->get_leaves().empty())
            {
                m_focused_pane = m_pane_tree->get_leaves().front().first;
            }
        }

        if (m_focused_pane == nullptr || !m_focused_pane->is_leaf())
        {
            return;
        }

        m_focused_pane->add_tab(id);
        m_focused_pane->set_active_tab(id);
    }

    void Shell::open_tab_split(const TabId id, const SplitterAxis axis, const bool place_after)
    {
        if (!m_pane_tree || m_focused_pane == nullptr || !m_focused_pane->is_leaf())
        {
            open_tab(id);
            return;
        }

        const bool new_pane_first = !place_after;
        m_focused_pane->split(split_kind_for_axis(axis), id, new_pane_first);
        m_focused_pane = new_pane_first ? m_focused_pane->get_first() : m_focused_pane->get_second();
    }

    void Shell::close_tab(const TabId id)
    {
        if (!m_pane_tree || m_focused_pane == nullptr || !m_focused_pane->is_leaf())
        {
            return;
        }

        m_focused_pane->remove_tab(id);
        m_pane_tree->collapse_empty_panes();
        const ViewportRect window{
            .x = 0,
            .y = 0,
            .width = m_swapchain.get_extent().width,
            .height = m_swapchain.get_extent().height
        };
        m_pane_tree->update_layout(compute_center_area(window));
        const auto leaves = m_pane_tree->get_leaves();
        m_focused_pane = leaves.empty() ? m_pane_tree->get_root() : leaves.front().first;
    }

    void Shell::focus_pane(Pane* pane)
    {
        if (pane != nullptr && pane->is_leaf())
        {
            m_focused_pane = pane;
        }
    }

    void Shell::focus_tab(const TabId id)
    {
        if (!m_pane_tree)
        {
            return;
        }

        if (Pane* pane = m_pane_tree->find_pane_for_tab(id); pane != nullptr)
        {
            m_focused_pane = pane;
            pane->set_active_tab(id);
            return;
        }

        open_tab(id);
    }

    void Shell::push_notification(std::string message, const NotificationSeverity severity)
    {
        if (m_status_bar)
        {
            m_status_bar->push_notification(std::move(message), severity);
        }
    }

    tabs::PlanetariumTab& Shell::get_planetarium_tab() noexcept
    {
        return *m_planetarium;
    }

    tabs::EncyclopediaTab& Shell::get_encyclopedia_tab() noexcept
    {
        return *m_encyclopedia;
    }

    tabs::BaseTab& Shell::get_base_tab() noexcept
    {
        return *m_base;
    }

    tabs::ArchiveTab& Shell::get_archive_tab() noexcept
    {
        return *m_archive_tab;
    }

    tabs::ImagingTab& Shell::get_imaging_tab() noexcept
    {
        return *m_imaging;
    }

    TabContent& Shell::tab_for(TabId id) noexcept
    {
        TabContent* tab = m_tabs_by_id[static_cast<u32>(id)];
        if (tab == nullptr)
        {
            return *m_planetarium;
        }
        return *tab;
    }

    void Shell::save_layout()
    {
        if (!m_pane_tree)
        {
            return;
        }

        try
        {
            json root;
            root["version"] = 1;
            root["active_observer_index"] = m_observer_registry.get_active_index();
            root["atmosphere_preference"] = m_planetarium->is_atmosphere_on();
            root["selected_object_id"] = nullptr;
            root["layout"] = json::object({{"root", serialize_pane(m_pane_tree->get_root())}});

            const std::filesystem::path path = core::user_data_save_dir() / "shell.json";
            std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
            if (!ofs.is_open())
            {
                PLX_CORE_WARN("Failed to open shell layout path for writing: {}", path.string());
                return;
            }

            ofs << root.dump(2);
        }
        catch (const std::exception& ex)
        {
            PLX_CORE_WARN("Failed to save shell layout: {}", ex.what());
        }
    }

    void Shell::reset_default_layout()
    {
        m_pane_tree = std::make_unique<PaneTree>(TabId::Planetarium);
        const auto leaves = m_pane_tree->get_leaves();
        if (!leaves.empty())
        {
            m_focused_pane = leaves.front().first;
        }
        else
        {
            m_focused_pane = m_pane_tree->get_root();
        }
    }

    void Shell::load_layout_or_default()
    {
        const auto reset_with_warning = [this]()
        {
            reset_default_layout();
            push_notification("Shell layout reset (file missing or invalid)", NotificationSeverity::Warning);
        };

        const std::filesystem::path path = core::user_data_save_dir() / "shell.json";
        if (!std::filesystem::exists(path))
        {
            reset_with_warning();
            return;
        }

        try
        {
            std::ifstream ifs(path, std::ios::binary);
            if (!ifs.is_open())
            {
                reset_with_warning();
                return;
            }

            const json root = json::parse(ifs);
            if (!root.is_object() || root.value("version", 0) != 1)
            {
                reset_with_warning();
                return;
            }

            if (!root.contains("layout") || !root["layout"].is_object() || !root["layout"].contains("root"))
            {
                reset_with_warning();
                return;
            }

            m_pane_tree = std::make_unique<PaneTree>(TabId::Planetarium);
            if (!apply_layout_to_pane(m_pane_tree->get_root(), root["layout"]["root"]))
            {
                reset_with_warning();
                return;
            }

            const i32 active_index = root.value("active_observer_index", 0);
            if (active_index >= 0 && active_index < static_cast<i32>(m_observer_registry.size()))
            {
                m_observer_registry.set_active(active_index);
            }
            else
            {
                m_observer_registry.set_active(0);
                push_notification("Observer index invalid in shell layout, using default", NotificationSeverity::Warning);
            }

            m_planetarium->set_atmosphere(root.value("atmosphere_preference", true));

            const ViewportRect window{
                .x = 0,
                .y = 0,
                .width = m_swapchain.get_extent().width,
                .height = m_swapchain.get_extent().height
            };
            m_pane_tree->update_layout(compute_center_area(window));
            const auto leaves = m_pane_tree->get_leaves();
            m_focused_pane = leaves.empty() ? m_pane_tree->get_root() : leaves.front().first;
        }
        catch (const std::exception&)
        {
            reset_with_warning();
        }
    }
}

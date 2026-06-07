#pragma once

#include "core/types.hpp"
#include "ui/shell/pane_tree.hpp"
#include "ui/shell/sidebar.hpp"
#include "ui/shell/status_bar.hpp"
#include "ui/shell/tab_id.hpp"
#include "ui/shell/top_bar.hpp"
#include "ui/shell/viewport_rect.hpp"

#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace parallax::astro { class ObserverRegistry; }
namespace parallax::knowledge { class KnowledgeDatabase; }
namespace parallax::observation { class DataArchive; class SessionScheduler; }
namespace parallax::rendering { class LineRenderer; }
namespace parallax::universe { class Universe; }
namespace parallax::vulkan { class Context; class Swapchain; }
namespace parallax::core { class Input; }
namespace parallax::ui { class BitmapFont; class PanelSystem; class Selection; }
namespace parallax::ui::tabs
{
    class PlanetariumTab;
    class ArchiveTab;
    class EncyclopediaTab;
    class BaseTab;
    class ImagingTab;
    class SpectroscopyTab;
    class AnalysisTab;
    class AllskyTab;
}

namespace parallax::ui::shell
{
    struct ShellCallbacks
    {
        std::function<void()> on_pause_toggle;
        std::function<void(f64)> on_time_scale_set;
        std::function<void()> on_time_scale_up;
        std::function<void()> on_time_scale_down;
        std::function<void()> on_time_reset;
    };

    class Shell
    {
    public:
        Shell(BitmapFont& font,
              rendering::LineRenderer& line_renderer,
              PanelSystem& panel_system,
              const vulkan::Context& context,
              vulkan::Swapchain& swapchain,
              VkRenderPass render_pass,
              const std::filesystem::path& shader_dir,
              universe::Universe& universe,
              knowledge::KnowledgeDatabase& knowledge,
              observation::DataArchive& archive,
              observation::SessionScheduler& scheduler,
              astro::ObserverRegistry& observer_registry,
              Selection& selection,
              core::Input& input,
              f64& julian_date,
              f64& time_scale);
        ~Shell();

        Shell(const Shell&)            = delete;
        Shell& operator=(const Shell&) = delete;
        Shell(Shell&&)                 = delete;
        Shell& operator=(Shell&&)      = delete;

        void init();
        void set_callbacks(ShellCallbacks callbacks);
        void update(f64 delta_time);
        void render(VkCommandBuffer cmd, const ViewportRect& window_rect);
        bool on_input(const InputEvent& event);

        void open_tab(TabId id);
        void open_tab_split(TabId id, SplitterAxis axis, bool place_after);
        void close_tab(TabId id);
        void focus_pane(Pane* pane);
        void focus_tab(TabId id);

        void push_notification(std::string message,
                               NotificationSeverity severity = NotificationSeverity::Info);

        [[nodiscard]] tabs::PlanetariumTab& get_planetarium_tab() noexcept;
        [[nodiscard]] tabs::EncyclopediaTab& get_encyclopedia_tab() noexcept;
        [[nodiscard]] tabs::BaseTab& get_base_tab() noexcept;
        [[nodiscard]] tabs::ArchiveTab& get_archive_tab() noexcept;

        void save_layout();

    private:
        [[nodiscard]] ViewportRect compute_center_area(const ViewportRect& window) const;
        [[nodiscard]] InputEvent make_local_event(const InputEvent& screen_event, const ViewportRect& rect) const;
        [[nodiscard]] TabContent& tab_for(TabId id) noexcept;
        void bind_callbacks();
        void update_chrome_state(f64 delta_time);
        void load_layout_or_default();
        void reset_default_layout();
        void apply_global_shortcuts();
        void apply_planetarium_shortcuts();
        [[nodiscard]] bool handle_splitter_drag(const InputEvent& event, const ViewportRect& center_area);

        BitmapFont& m_font;
        rendering::LineRenderer& m_line_renderer;
        PanelSystem& m_panel_system;
        const vulkan::Context& m_context;
        vulkan::Swapchain& m_swapchain;
        VkRenderPass m_render_pass = VK_NULL_HANDLE;
        std::filesystem::path m_shader_dir;

        universe::Universe& m_universe;
        knowledge::KnowledgeDatabase& m_knowledge;
        observation::DataArchive& m_archive;
        observation::SessionScheduler& m_scheduler;
        astro::ObserverRegistry& m_observer_registry;
        Selection& m_selection;
        core::Input& m_input;
        f64& m_julian_date;
        f64& m_time_scale;

        std::unique_ptr<Sidebar> m_sidebar;
        std::unique_ptr<TopBar> m_top_bar;
        std::unique_ptr<StatusBar> m_status_bar;

        std::unique_ptr<PaneTree> m_pane_tree;
        Pane* m_focused_pane = nullptr;
        Pane* m_dragging_splitter = nullptr;

        std::unique_ptr<tabs::PlanetariumTab> m_planetarium;
        std::unique_ptr<tabs::ImagingTab> m_imaging;
        std::unique_ptr<tabs::SpectroscopyTab> m_spectroscopy;
        std::unique_ptr<tabs::AnalysisTab> m_analysis;
        std::unique_ptr<tabs::ArchiveTab> m_archive_tab;
        std::unique_ptr<tabs::EncyclopediaTab> m_encyclopedia;
        std::unique_ptr<tabs::AllskyTab> m_allsky;
        std::unique_ptr<tabs::BaseTab> m_base;

        std::array<TabContent*, kTabIdCount> m_tabs_by_id{};
        ShellCallbacks m_callbacks{};

        std::size_t m_last_archive_size = 0;
    };
}

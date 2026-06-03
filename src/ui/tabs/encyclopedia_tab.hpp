#pragma once

#include "core/types.hpp"
#include "observation/data_archive.hpp"
#include "ui/selection.hpp"
#include "ui/shell/tab_id.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace parallax::knowledge
{
    class KnowledgeDatabase;
}

namespace parallax::rendering
{
    class LineRenderer;
}

namespace parallax::ui
{
    class BitmapFont;
}

namespace parallax::universe
{
    class Universe;
}

namespace parallax::vulkan
{
    class Swapchain;
}

namespace parallax::ui::tabs
{
    class EncyclopediaTab final : public shell::TabContent
    {
    public:
        enum class MinLevel : u8
        {
            L1,
            L2,
            L3,
            L4
        };

        enum class SortKey : u8
        {
            Name,
            Distance,
            Magnitude,
            DiscoveryDate
        };

        enum class SortDir : u8
        {
            Asc,
            Desc
        };

        struct TypeFilters
        {
            bool stars       = true;
            bool dsos        = true;
            bool solar       = true;
            bool discovered  = true;
            bool candidates  = true;
        };

        struct ShellHooks
        {
            std::function<void(u64)> locate_in_planetarium;   // TODO(Task 9.11): wired by Shell.
            std::function<void(u64)> track_in_planetarium;    // TODO(Task 9.11): wired by Shell.
            std::function<void(u64)> open_observe_for;        // TODO(Task 9.11): wired by Shell.
        };

        EncyclopediaTab(BitmapFont& font,
                        rendering::LineRenderer& line_renderer,
                        vulkan::Swapchain& swapchain,
                        const universe::Universe& universe,
                        const knowledge::KnowledgeDatabase& knowledge,
                        const observation::DataArchive& archive,
                        Selection& selection);
        ~EncyclopediaTab() override = default;

        EncyclopediaTab(const EncyclopediaTab&)            = delete;
        EncyclopediaTab& operator=(const EncyclopediaTab&) = delete;
        EncyclopediaTab(EncyclopediaTab&&)                 = delete;
        EncyclopediaTab& operator=(EncyclopediaTab&&)      = delete;

        void update(f64 delta_time) override;
        void render(VkCommandBuffer cmd, const shell::ViewportRect& rect) override;
        void on_input(const shell::InputEvent& event, const shell::ViewportRect& rect) override;

        [[nodiscard]] shell::TabId get_id() const override;

        void set_shell_hooks(ShellHooks hooks);

    private:
        struct CachedRow
        {
            u64 id = 0;
            std::string designation;
            std::string subtitle;
            char status_glyph = '?';
            f64 distance_key = 0.0;
            f32 magnitude_key = 0.0f;
            f64 discovery_key = 0.0;
        };

        struct PropertyLine
        {
            std::string label;
            std::string value;
            bool dim_hint = false;
        };

        struct ObjectDetail
        {
            std::string designation;
            std::string status_text;
            std::string status_level;
            Vec4f status_bg{0.0f, 0.0f, 0.0f, 0.0f};

            std::vector<PropertyLine> position_rows;
            std::vector<PropertyLine> photometry_rows;
            std::vector<PropertyLine> type_rows;
            std::vector<PropertyLine> discovery_rows;
            std::vector<PropertyLine> discover_rows;

            i32 observation_count = 0;
        };

        void rebuild_cache(const shell::ViewportRect& rect);
        void rebuild_detail_cache();
        void sync_selection_from_global(const shell::ViewportRect& rect);
        void sync_global_selection(u64 object_id);
        void ensure_selected_visible(const shell::ViewportRect& rect);

        [[nodiscard]] bool passes_filters(const universe::CelestialObject& object) const;
        [[nodiscard]] bool matches_search(const CachedRow& row) const;
        [[nodiscard]] bool row_in_view(i32 absolute_row, i32 visible_rows) const;
        [[nodiscard]] i32 visible_row_count(const shell::ViewportRect& rect) const;

        [[nodiscard]] static std::string format_ra(f64 ra_rad);
        [[nodiscard]] static std::string format_dec(f64 dec_rad);
        [[nodiscard]] static std::string format_alt(f64 alt_rad);
        [[nodiscard]] static std::string format_az(f64 az_rad);

        BitmapFont& m_font;
        rendering::LineRenderer& m_line_renderer;
        vulkan::Swapchain& m_swapchain;
        const universe::Universe& m_universe;
        const knowledge::KnowledgeDatabase& m_knowledge;
        const observation::DataArchive& m_archive;
        Selection& m_selection;

        TypeFilters m_filters;
        MinLevel m_min_level = MinLevel::L1;
        std::string m_search_query;
        SortKey m_sort_key = SortKey::Name;
        SortDir m_sort_dir = SortDir::Asc;

        std::optional<u64> m_selected_object;
        i32 m_scroll_row = 0;
        i32 m_hovered_row = -1;
        bool m_search_focused = false;

        std::vector<CachedRow> m_cached_rows;
        ObjectDetail m_detail_cache;
        shell::ViewportRect m_last_rect{};

        ShellHooks m_hooks;
    };
}

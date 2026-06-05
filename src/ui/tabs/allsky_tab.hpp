/// @file allsky_tab.hpp
/// @brief Placeholder tab for all-sky camera view (Sprint 10).
///
/// SPRINT 09 Task 9.9 — Placeholder tabs.
#pragma once

#include "ui/shell/tab_id.hpp"
#include "ui/shell/viewport_rect.hpp"

namespace parallax::ui
{
    class BitmapFont;
}

namespace parallax::ui::tabs
{
    class AllskyTab final : public shell::TabContent
    {
    public:
        explicit AllskyTab(BitmapFont& font);
        ~AllskyTab() override = default;

        AllskyTab(const AllskyTab&)            = delete;
        AllskyTab& operator=(const AllskyTab&) = delete;
        AllskyTab(AllskyTab&&)                 = delete;
        AllskyTab& operator=(AllskyTab&&)      = delete;

        void update(f64 delta_time) override;
        void render(VkCommandBuffer cmd, const shell::ViewportRect& viewport) override;
        void on_input(const shell::InputEvent& event, const shell::ViewportRect& viewport) override;

        [[nodiscard]] shell::TabId get_id() const override
        {
            return shell::TabId::AllSky;
        }

    private:
        BitmapFont& m_font;
    };
}

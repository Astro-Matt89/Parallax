/// @file spectroscopy_tab.hpp
/// @brief Placeholder tab for spectroscopy visualisation (Sprint 11).
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
    class SpectroscopyTab final : public shell::TabContent
    {
    public:
        explicit SpectroscopyTab(BitmapFont& font);
        ~SpectroscopyTab() override = default;

        SpectroscopyTab(const SpectroscopyTab&)            = delete;
        SpectroscopyTab& operator=(const SpectroscopyTab&) = delete;
        SpectroscopyTab(SpectroscopyTab&&)                 = delete;
        SpectroscopyTab& operator=(SpectroscopyTab&&)      = delete;

        void update(f64 delta_time) override;
        void render(VkCommandBuffer cmd, const shell::ViewportRect& viewport) override;
        void on_input(const shell::InputEvent& event, const shell::ViewportRect& viewport) override;

        [[nodiscard]] shell::TabId get_id() const override
        {
            return shell::TabId::Spectroscopy;
        }

    private:
        BitmapFont& m_font;
    };
}

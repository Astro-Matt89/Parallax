/// @file imaging_tab.hpp
/// @brief Placeholder tab for the live telescope feed (Sprint 10).
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
    class ImagingTab final : public shell::TabContent
    {
    public:
        explicit ImagingTab(BitmapFont& font);
        ~ImagingTab() override = default;

        ImagingTab(const ImagingTab&)            = delete;
        ImagingTab& operator=(const ImagingTab&) = delete;
        ImagingTab(ImagingTab&&)                 = delete;
        ImagingTab& operator=(ImagingTab&&)      = delete;

        void update(f64 delta_time) override;
        void render(VkCommandBuffer cmd, const shell::ViewportRect& viewport) override;
        void on_input(const shell::InputEvent& event, const shell::ViewportRect& viewport) override;

        [[nodiscard]] shell::TabId get_id() const override
        {
            return shell::TabId::Imaging;
        }

    private:
        BitmapFont& m_font;
    };
}

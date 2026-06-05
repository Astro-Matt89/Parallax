/// @file analysis_tab.hpp
/// @brief Placeholder tab for the data analysis workspace (Sprint 10+).
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
    class AnalysisTab final : public shell::TabContent
    {
    public:
        explicit AnalysisTab(BitmapFont& font);
        ~AnalysisTab() override = default;

        AnalysisTab(const AnalysisTab&)            = delete;
        AnalysisTab& operator=(const AnalysisTab&) = delete;
        AnalysisTab(AnalysisTab&&)                 = delete;
        AnalysisTab& operator=(AnalysisTab&&)      = delete;

        void update(f64 delta_time) override;
        void render(VkCommandBuffer cmd, const shell::ViewportRect& viewport) override;
        void on_input(const shell::InputEvent& event, const shell::ViewportRect& viewport) override;

        [[nodiscard]] shell::TabId get_id() const override
        {
            return shell::TabId::Analysis;
        }

    private:
        BitmapFont& m_font;
    };
}

#pragma once

#include "astro/observer_registry.hpp"
#include "observation/data_archive.hpp"
#include "ui/shell/tab_id.hpp"
#include "ui/shell/viewport_rect.hpp"

#include <functional>
#include <string>
#include <vector>

namespace parallax::ui
{
    class BitmapFont;
}

namespace parallax::ui::tabs
{
    /// @brief Lunar/Earth base management tab.
    ///
    /// SPRINT 09 Task 9.8 — pure-UI, reads ObserverRegistry, DataArchive,
    /// and (eventually) the instrument registry. For Sprint 09 the
    /// instrument list is hardcoded to a single "Magic Instrument" row.
    ///
    /// The only mutating action is clicking a row in AVAILABLE LOCATIONS,
    /// which calls ObserverRegistry::set_active(i). Application does NOT yet
    /// react to that change — Task 9.10 wires the active observer back into
    /// the skychart and atmosphere logic.
    class BaseTab final : public shell::TabContent
    {
    public:
        struct ShellHooks
        {
            /// Fired when the user clicks an instrument row. Stable id string.
            std::function<void(std::string instrument_id)> on_instrument_selected;
        };

        BaseTab(BitmapFont& font,
                astro::ObserverRegistry& registry,
                const observation::DataArchive& archive);
        ~BaseTab() override = default;

        BaseTab(const BaseTab&)            = delete;
        BaseTab& operator=(const BaseTab&) = delete;
        BaseTab(BaseTab&&)                 = delete;
        BaseTab& operator=(BaseTab&&)      = delete;

        void update(f64 delta_time) override;
        void render(VkCommandBuffer cmd, const shell::ViewportRect& rect) override;
        void on_input(const shell::InputEvent& event, const shell::ViewportRect& rect) override;

        [[nodiscard]] shell::TabId get_id() const override;

        void set_shell_hooks(ShellHooks hooks);

    private:
        BitmapFont& m_font;
        astro::ObserverRegistry& m_registry;
        const observation::DataArchive& m_archive;
        ShellHooks m_hooks{};

        i32 m_hovered_location = -1;
        i32 m_hovered_instrument = -1;
        std::vector<shell::ViewportRect> m_location_rows;
        std::vector<shell::ViewportRect> m_instrument_rows;
    };
}

/// @file tab_id.hpp
/// @brief Tab identity enum and TabContent abstract base class.
///
/// SPRINT 09 Task 9.1 — Shell: Core Types and Layout.
///
/// The shell hosts a fixed set of eight tabs (see CLAUDE.md §8 and
/// docs/sprints/sprint_09.md). Each tab has:
///   - A compile-time identity (@ref TabId) used as a key everywhere
///     (Pane tab lists, Sidebar buttons, Shell tab-content map).
///   - A runtime implementation derived from @ref TabContent that owns the
///     tab's state and knows how to render and handle input within a
///     ViewportRect.
///
/// All TabContent instances are created once at Shell startup (Task 9.11)
/// and kept alive for the entire game session, even when not visible.
/// `update()` is called every frame regardless of visibility, while
/// `render()` is only called for tabs that are the active tab of a
/// currently-visible leaf pane.

#pragma once

#include "core/types.hpp"
#include "ui/shell/shell_types.hpp"
#include "ui/shell/viewport_rect.hpp"

#include <vulkan/vulkan.h>

#include <string>

namespace parallax::ui::shell
{
    // ------------------------------------------------------------------
    // TabId
    // ------------------------------------------------------------------

    /// @brief Identifies one of the eight built-in tabs of the Parallax shell.
    ///
    /// The numeric values are stable: they are used as keys in the Shell's
    /// tab-content map and (in Task 9.11) persisted to the shell layout file.
    /// Do NOT renumber existing entries; append new tabs at the end.
    enum class TabId : u8
    {
        Planetarium  = 0,  ///< Skychart view (migrated from Sprint 05).
        Imaging      = 1,  ///< Live telescope feed (placeholder until Sprint 10).
        Spectroscopy = 2,  ///< Spectrum visualisation (placeholder until Sprint 11).
        Analysis     = 3,  ///< Data analysis workspace (placeholder until Sprint 10+).
        Archive      = 4,  ///< Data archive (migrated from DataArchivePanel).
        Encyclopedia = 5,  ///< Knowledge browser (replaces InfoPanel).
        AllSky       = 6,  ///< All-sky camera (placeholder until Sprint 10).
        Base         = 7   ///< Lunar base management.
    };

    /// @brief Number of distinct TabId values. Update if a new tab is added.
    inline constexpr u32 kTabIdCount = 8;

    /// @return The canonical short name of @p id used in sidebars and tab
    ///         headers (uppercase, matches the design in sprint_09.md).
    [[nodiscard]] constexpr const char* to_display_name(TabId id) noexcept
    {
        switch (id)
        {
            case TabId::Planetarium:  return "PLANETARIUM";
            case TabId::Imaging:      return "IMAGING";
            case TabId::Spectroscopy: return "SPECTROSCOPY";
            case TabId::Analysis:     return "ANALYSIS";
            case TabId::Archive:      return "ARCHIVE";
            case TabId::Encyclopedia: return "ENCYCLOPEDIA";
            case TabId::AllSky:       return "ALL-SKY";
            case TabId::Base:         return "BASE";
        }
        return "UNKNOWN";
    }

    /// @return The stable, lower_snake_case key used for serialising @p id
    ///         to the shell layout JSON file (Task 9.11).
    [[nodiscard]] constexpr const char* to_persistence_key(TabId id) noexcept
    {
        switch (id)
        {
            case TabId::Planetarium:  return "planetarium";
            case TabId::Imaging:      return "imaging";
            case TabId::Spectroscopy: return "spectroscopy";
            case TabId::Analysis:     return "analysis";
            case TabId::Archive:      return "archive";
            case TabId::Encyclopedia: return "encyclopedia";
            case TabId::AllSky:       return "allsky";
            case TabId::Base:         return "base";
        }
        return "unknown";
    }

    // ------------------------------------------------------------------
    // TabContent
    // ------------------------------------------------------------------

    /// @brief Abstract base class for everything that can live inside a tab.
    ///
    /// Each concrete tab (PlanetariumTab, ArchiveTab, …) inherits from this
    /// class and is owned by the Shell for the full session. The contract is:
    ///
    ///   - @ref update is called every frame for every tab, even those not
    ///     currently visible. This is what lets PlanetariumTab keep tracking
    ///     a target while the player browses the Archive tab.
    ///
    ///   - @ref render is called only when this tab is the active tab of a
    ///     visible leaf pane. The provided @p viewport is in window
    ///     framebuffer pixels and the implementation MUST clip its drawing
    ///     to it (vkCmdSetViewport + vkCmdSetScissor).
    ///
    ///   - @ref on_input is called only when this tab is the active tab of
    ///     the currently *focused* pane and the cursor is inside its
    ///     viewport. Coordinates inside @p event are already viewport-local.
    ///
    ///   - @ref get_display_name and @ref get_id are pure metadata accessors
    ///     used by sidebars, tab headers, and the persistence layer.
    ///
    /// Implementations must be safe to call @ref update on every frame from
    /// the main thread; Vulkan resource management belongs in @ref render.
    class TabContent
    {
    public:
        virtual ~TabContent() = default;

        TabContent(const TabContent&)            = delete;
        TabContent& operator=(const TabContent&) = delete;
        TabContent(TabContent&&)                 = delete;
        TabContent& operator=(TabContent&&)      = delete;

        /// @brief Advance the tab's internal simulation/animation state.
        /// @param delta_time Seconds elapsed since the previous frame.
        ///
        /// Called every frame for every tab regardless of visibility.
        /// Implementations should keep this cheap and avoid Vulkan calls.
        virtual void update(f64 delta_time) = 0;

        /// @brief Record draw commands for this tab into @p cmd.
        /// @param cmd      Command buffer already in a render pass; the
        ///                 caller has set the full-window viewport/scissor.
        /// @param viewport Framebuffer region this tab is allowed to draw into.
        ///
        /// Only called when this tab is visible. Implementations must set
        /// the Vulkan viewport and scissor to @p viewport before drawing
        /// and should leave them set (the Shell restores them after).
        virtual void render(VkCommandBuffer cmd, const ViewportRect& viewport) = 0;

        /// @brief Handle user input directed at this tab.
        /// @param event    Viewport-local input snapshot for this frame.
        /// @param viewport The viewport @p event was translated against.
        ///
        /// Only called when this tab is the active tab of the focused pane.
        virtual void on_input(const InputEvent& event, const ViewportRect& viewport) = 0;

        /// @return Human-readable name shown in tab headers and the sidebar.
        ///         Defaults to @ref to_display_name(get_id()); override for
        ///         dynamic titles (e.g. "PLANETARIUM — Orion").
        [[nodiscard]] virtual std::string get_display_name() const
        {
            return to_display_name(get_id());
        }

        /// @return The stable identity of this tab.
        [[nodiscard]] virtual TabId get_id() const = 0;

    protected:
        TabContent() = default;
    };
}

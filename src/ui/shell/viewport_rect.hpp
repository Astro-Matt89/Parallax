/// @file viewport_rect.hpp
/// @brief Rectangular framebuffer region used to render a tab pane.
///
/// SPRINT 09 Task 9.1 — Shell: Core Types and Layout.
///
/// A ViewportRect describes the area, in window framebuffer pixels, that a
/// pane (and therefore the tab it currently displays) occupies. All shell
/// renderers and input handlers operate relative to a ViewportRect rather
/// than the full framebuffer, which is what allows the central area to be
/// recursively split into multiple panes (see Task 9.2 — PaneTree).
///
/// Coordinate convention:
///   - Origin (0, 0) is the top-left of the window framebuffer.
///   - +x is right, +y is down (matches SDL2 and Vulkan viewport conventions).
///   - x, y, width, height are expressed in pixels (not normalised).
///
/// Header-only by design: this is a value type used throughout the shell.

#pragma once

#include "core/types.hpp"

namespace parallax::ui::shell
{
    /// @brief Rectangular region of the window framebuffer, in pixels.
    ///
    /// Used by every shell component (panes, tabs, sidebar, top/status bars)
    /// to express "the area I am allowed to render into". Renderers must
    /// translate their drawing into this rect (typically via
    /// vkCmdSetViewport + vkCmdSetScissor) and input handlers must reject
    /// or translate cursor positions outside of it.
    struct ViewportRect
    {
        u32 x      = 0;  ///< Left edge in framebuffer pixels.
        u32 y      = 0;  ///< Top edge in framebuffer pixels.
        u32 width  = 0;  ///< Width in pixels.
        u32 height = 0;  ///< Height in pixels.

        /// @return Right edge (one past the last pixel).
        [[nodiscard]] constexpr u32 right() const noexcept
        {
            return x + width;
        }

        /// @return Bottom edge (one past the last pixel).
        [[nodiscard]] constexpr u32 bottom() const noexcept
        {
            return y + height;
        }

        /// @return Aspect ratio (width / height); 0 if the rect is degenerate.
        [[nodiscard]] constexpr f32 aspect() const noexcept
        {
            return (height == 0) ? 0.0f : static_cast<f32>(width) / static_cast<f32>(height);
        }

        /// @return True if the rect has non-zero area.
        [[nodiscard]] constexpr bool is_valid() const noexcept
        {
            return width > 0 && height > 0;
        }

        /// @return True if the given framebuffer pixel falls inside this rect.
        [[nodiscard]] constexpr bool contains(i32 px, i32 py) const noexcept
        {
            return px >= static_cast<i32>(x)
                && py >= static_cast<i32>(y)
                && px <  static_cast<i32>(x + width)
                && py <  static_cast<i32>(y + height);
        }

        /// @return True if this rect is identical to @p other.
        [[nodiscard]] constexpr bool operator==(const ViewportRect& other) const noexcept = default;
    };
}

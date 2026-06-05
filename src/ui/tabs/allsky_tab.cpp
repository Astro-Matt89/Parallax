#include "ui/tabs/allsky_tab.hpp"

#include "ui/font.hpp"
#include "ui/font_data.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <string_view>

namespace parallax::ui::tabs
{
    namespace
    {
        // TODO: deduplicate shell colour palette.
        constexpr Vec4f kBackgroundColor{0.02f, 0.06f, 0.02f, 1.0f};
        constexpr Vec3f kContentColor{0.40f, 1.00f, 0.40f};
        constexpr Vec3f kInactiveColor{0.25f, 0.45f, 0.25f};
        constexpr Vec3f kHeadlineColor{0.50f, 1.00f, 0.50f};

        constexpr u32 kArtToHeadlineGapPx  = 16;
        constexpr u32 kHeadlineToBodyGapPx = 8;
        constexpr u32 kMinSideMarginPx     = 8;

        void apply_viewport(VkCommandBuffer cmd, const shell::ViewportRect& rect)
        {
            VkViewport viewport{};
            viewport.x = static_cast<f32>(rect.x);
            viewport.y = static_cast<f32>(rect.y);
            viewport.width = static_cast<f32>(rect.width);
            viewport.height = static_cast<f32>(rect.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);

            VkRect2D scissor{};
            scissor.offset = {static_cast<i32>(rect.x), static_cast<i32>(rect.y)};
            scissor.extent = {rect.width, rect.height};
            vkCmdSetScissor(cmd, 0, 1, &scissor);
        }

        void clear_background(VkCommandBuffer cmd, const shell::ViewportRect& rect)
        {
            VkClearAttachment clear_attachment{};
            clear_attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            clear_attachment.colorAttachment = 0;
            clear_attachment.clearValue.color = {
                {kBackgroundColor.r, kBackgroundColor.g, kBackgroundColor.b, kBackgroundColor.a}
            };

            VkClearRect clear_rect{};
            clear_rect.rect.offset = {static_cast<i32>(rect.x), static_cast<i32>(rect.y)};
            clear_rect.rect.extent = {rect.width, rect.height};
            clear_rect.baseArrayLayer = 0;
            clear_rect.layerCount = 1;

            vkCmdClearAttachments(cmd, 1, &clear_attachment, 1, &clear_rect);
        }

        [[nodiscard]] i32 text_width_px(const std::string_view text)
        {
            return static_cast<i32>(text.size()) * kFontGlyphW;
        }

        void draw_placeholder(
            VkCommandBuffer cmd,
            const shell::ViewportRect& rect,
            BitmapFont& font,
            std::span<const std::string_view> ascii_art,
            const std::string_view headline,
            std::span<const std::string_view> description_lines)
        {
            apply_viewport(cmd, rect);
            clear_background(cmd, rect);

            const i32 line_h = kFontGlyphH;
            const i32 art_h = static_cast<i32>(ascii_art.size()) * line_h;
            const i32 description_h = static_cast<i32>(description_lines.size()) * line_h;
            const i32 block_h = art_h
                + static_cast<i32>(kArtToHeadlineGapPx)
                + line_h
                + static_cast<i32>(kHeadlineToBodyGapPx)
                + description_h;

            i32 y = static_cast<i32>(rect.y) + (static_cast<i32>(rect.height) - block_h) / 2;
            const i32 min_x = static_cast<i32>(rect.x + kMinSideMarginPx);

            const auto draw_centered = [&](const std::string_view text, const Vec3f color)
            {
                const i32 centered_x = static_cast<i32>(rect.x)
                    + (static_cast<i32>(rect.width) - text_width_px(text)) / 2;
                const i32 x = std::max(centered_x, min_x);
                font.draw_text(std::string{text}, static_cast<f32>(x), static_cast<f32>(y), 1.0f, color);
                y += line_h;
            };

            for (const std::string_view line : ascii_art)
            {
                draw_centered(line, kInactiveColor);
            }

            y += static_cast<i32>(kArtToHeadlineGapPx);
            draw_centered(headline, kHeadlineColor);
            y += static_cast<i32>(kHeadlineToBodyGapPx);

            for (const std::string_view line : description_lines)
            {
                draw_centered(line, kContentColor);
            }
        }
    }

    AllskyTab::AllskyTab(BitmapFont& font)
        : m_font(font)
    {
    }

    void AllskyTab::update(f64 /*delta_time*/)
    {
    }

    void AllskyTab::render(VkCommandBuffer cmd, const shell::ViewportRect& viewport)
    {
        if (!viewport.is_valid())
        {
            return;
        }

        constexpr std::array<std::string_view, 7> kAsciiArt = {
            "       . . .",
            "     .   *   .",
            "    . *     . .",
            "   .  N S E W  .",
            "    . *     . .",
            "     .   *   .",
            "       . . ."
        };

        constexpr std::array<std::string_view, 3> kDescription = {
            "All-sky camera view.",
            "Wide-field monitor of the whole",
            "hemisphere visible from this site."
        };

        draw_placeholder(
            cmd,
            viewport,
            m_font,
            std::span<const std::string_view>{kAsciiArt},
            "Available in Sprint 10",
            std::span<const std::string_view>{kDescription});
    }

    void AllskyTab::on_input(const shell::InputEvent& /*event*/, const shell::ViewportRect& /*viewport*/)
    {
    }
}

#include "ui/tabs/base_tab.hpp"

#include "ui/font.hpp"
#include "ui/font_data.hpp"

#include <glm/trigonometric.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace parallax::ui::tabs
{
    namespace
    {
        constexpr u32 kPaddingX        = 8;
        constexpr u32 kHeaderH         = 24;
        constexpr u32 kSectionHeaderH  = 16;
        constexpr u32 kRowH            = 18;
        constexpr u32 kSectionGap      = 8;
        constexpr u32 kBorderWidth     = 1;

        constexpr f32 kGlyphW = 8.0f;

        // TODO: deduplicate shell colour palette.
        constexpr Vec4f kBackgroundColor    {0.02f, 0.06f, 0.02f, 1.0f};
        constexpr Vec4f kBorderColor        {0.20f, 0.80f, 0.20f, 1.0f};
        constexpr Vec3f kSectionHeaderColor {0.30f, 0.60f, 0.30f};
        constexpr Vec3f kContentColor       {0.40f, 1.00f, 0.40f};
        constexpr Vec3f kInactiveColor      {0.25f, 0.45f, 0.25f};
        constexpr Vec4f kHoverHighlight     {0.10f, 0.20f, 0.10f, 1.0f};
        constexpr Vec4f kSelectedHighlight  {0.15f, 0.35f, 0.15f, 1.0f};

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

        void clear_rect(VkCommandBuffer cmd, const shell::ViewportRect& rect, const Vec4f color)
        {
            if (!rect.is_valid())
            {
                return;
            }

            VkClearAttachment clear_attachment{};
            clear_attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            clear_attachment.colorAttachment = 0;
            clear_attachment.clearValue.color = {{color.r, color.g, color.b, color.a}};

            VkClearRect clear_rect{};
            clear_rect.rect.offset = {static_cast<i32>(rect.x), static_cast<i32>(rect.y)};
            clear_rect.rect.extent = {rect.width, rect.height};
            clear_rect.baseArrayLayer = 0;
            clear_rect.layerCount = 1;

            vkCmdClearAttachments(cmd, 1, &clear_attachment, 1, &clear_rect);
        }

        [[nodiscard]] shell::ViewportRect make_rect(const i32 x, const i32 y, const i32 width, const i32 height)
        {
            return {
                static_cast<u32>(std::max(0, x)),
                static_cast<u32>(std::max(0, y)),
                static_cast<u32>(std::max(0, width)),
                static_cast<u32>(std::max(0, height))
            };
        }

        void draw_border(VkCommandBuffer cmd, const shell::ViewportRect& rect)
        {
            if (!rect.is_valid())
            {
                return;
            }

            clear_rect(cmd, make_rect(static_cast<i32>(rect.x),
                                      static_cast<i32>(rect.y),
                                      static_cast<i32>(rect.width),
                                      static_cast<i32>(kBorderWidth)),
                       kBorderColor);
            clear_rect(cmd, make_rect(static_cast<i32>(rect.x),
                                      static_cast<i32>(rect.bottom() - kBorderWidth),
                                      static_cast<i32>(rect.width),
                                      static_cast<i32>(kBorderWidth)),
                       kBorderColor);
            clear_rect(cmd, make_rect(static_cast<i32>(rect.x),
                                      static_cast<i32>(rect.y),
                                      static_cast<i32>(kBorderWidth),
                                      static_cast<i32>(rect.height)),
                       kBorderColor);
            clear_rect(cmd, make_rect(static_cast<i32>(rect.right() - kBorderWidth),
                                      static_cast<i32>(rect.y),
                                      static_cast<i32>(kBorderWidth),
                                      static_cast<i32>(rect.height)),
                       kBorderColor);
        }

        [[nodiscard]] std::string format_signed_deg(const f64 radians)
        {
            return std::format("{:+.4f}\u00B0", glm::degrees(radians));
        }

        [[nodiscard]] std::string format_elevation(const f64 elevation_m)
        {
            return std::format("{:.0f} m", elevation_m);
        }

        [[nodiscard]] std::string format_atmosphere(const astro::ObserverLocation& observer)
        {
            if (!observer.has_atmosphere)
            {
                return "No (Vacuum)";
            }

            return std::format("Yes (Bortle {:.1f})", observer.bortle_scale);
        }
    }

    BaseTab::BaseTab(BitmapFont& font,
                     astro::ObserverRegistry& registry,
                     const observation::DataArchive& archive)
        : m_font(font)
        , m_registry(registry)
        , m_archive(archive)
    {
    }

    void BaseTab::update(const f64 delta_time)
    {
        static_cast<void>(delta_time);
    }

    void BaseTab::render(VkCommandBuffer cmd, const shell::ViewportRect& rect)
    {
        if (!rect.is_valid() || m_registry.size() == 0)
        {
            return;
        }

        apply_viewport(cmd, rect);
        clear_rect(cmd, rect, kBackgroundColor);
        draw_border(cmd, rect);

        m_location_rows.clear();
        m_instrument_rows.clear();

        const i32 content_x = static_cast<i32>(rect.x + kPaddingX);
        const i32 content_w = static_cast<i32>(rect.width) - static_cast<i32>(2 * kPaddingX + kBorderWidth);
        i32 cursor_y = static_cast<i32>(rect.y + kPaddingX);

        const astro::ObserverLocation& active = m_registry.get_active();
        const std::string header = active.name.empty()
            ? "BASE"
            : std::format("BASE: {}", active.name);
        m_font.draw_text(header, static_cast<f32>(content_x), static_cast<f32>(cursor_y), 1.0f, kContentColor);
        cursor_y += static_cast<i32>(kHeaderH);

        const auto draw_section_header = [&](const std::string_view title)
        {
            m_font.draw_text(std::string{title},
                             static_cast<f32>(content_x),
                             static_cast<f32>(cursor_y),
                             1.0f,
                             kSectionHeaderColor);
            cursor_y += static_cast<i32>(kSectionHeaderH);
        };

        const auto draw_kv_row = [&](const std::string_view label, const std::string& value)
        {
            m_font.draw_text(std::format("  {:<12}", label),
                             static_cast<f32>(content_x),
                             static_cast<f32>(cursor_y),
                             1.0f,
                             kInactiveColor);
            m_font.draw_text(value,
                             static_cast<f32>(content_x + 14 * kGlyphW),
                             static_cast<f32>(cursor_y),
                             1.0f,
                             kContentColor);
            cursor_y += static_cast<i32>(kRowH);
        };

        draw_section_header("LOCATION");
        draw_kv_row("Name:", active.name.empty() ? std::string{"Unknown"} : active.name);
        draw_kv_row("Parent:", astro::to_string(active.parent_body));
        draw_kv_row("Latitude:", format_signed_deg(active.latitude_rad));
        draw_kv_row("Longitude:", format_signed_deg(active.longitude_rad));
        draw_kv_row("Elevation:", format_elevation(active.elevation_m));
        draw_kv_row("Atmosphere:", format_atmosphere(active));
        cursor_y += static_cast<i32>(kSectionGap);

        draw_section_header("INSTRUMENTS AT THIS LOCATION");
        // TODO(Sprint 10+): pull instrument list from a per-location instrument registry.
        {
            const shell::ViewportRect row_rect = make_rect(content_x, cursor_y, content_w, static_cast<i32>(kRowH));
            m_instrument_rows.push_back(row_rect);

            if (m_hovered_instrument == 0)
            {
                clear_rect(cmd, row_rect, kHoverHighlight);
            }

            m_font.draw_text("  * Magic Instrument", static_cast<f32>(content_x), static_cast<f32>(cursor_y), 1.0f, kContentColor);
            const i32 status_x = content_x + std::max(0, content_w - static_cast<i32>(12 * kGlyphW));
            m_font.draw_text("idle", static_cast<f32>(status_x), static_cast<f32>(cursor_y), 1.0f, kInactiveColor);
            cursor_y += static_cast<i32>(kRowH);
        }
        cursor_y += static_cast<i32>(kSectionGap);

        draw_section_header("AVAILABLE LOCATIONS");
        {
            const std::span<const astro::ObserverLocation> locations = m_registry.get_all();
            const i32 active_index = m_registry.get_active_index();
            const i32 body_col_x = content_x + std::max(0, content_w - static_cast<i32>(12 * kGlyphW));

            for (i32 i = 0; i < static_cast<i32>(locations.size()); ++i)
            {
                const shell::ViewportRect row_rect = make_rect(content_x, cursor_y, content_w, static_cast<i32>(kRowH));
                m_location_rows.push_back(row_rect);

                if (i == active_index)
                {
                    clear_rect(cmd, row_rect, kSelectedHighlight);
                }
                else if (i == m_hovered_location)
                {
                    clear_rect(cmd, row_rect, kHoverHighlight);
                }

                const char* marker = (i == active_index) ? "(o)" : "( )";
                m_font.draw_text(std::format("  {} {}", marker, locations[static_cast<std::size_t>(i)].name),
                                 static_cast<f32>(content_x),
                                 static_cast<f32>(cursor_y),
                                 1.0f,
                                 kContentColor);

                m_font.draw_text(std::format("[{}]", astro::to_string(locations[static_cast<std::size_t>(i)].parent_body)),
                                 static_cast<f32>(body_col_x),
                                 static_cast<f32>(cursor_y),
                                 1.0f,
                                 kInactiveColor);
                cursor_y += static_cast<i32>(kRowH);
            }
        }
        cursor_y += static_cast<i32>(kSectionGap);

        draw_section_header("SYSTEMS");
        draw_kv_row("Power:", "100%");
        draw_kv_row("Storage:", std::format("{} records", m_archive.size()));
        draw_kv_row("Atmosphere:", "Sealed");
        // TODO(Sprint 10+): real systems telemetry.
        cursor_y += static_cast<i32>(kSectionGap);

        draw_section_header("NETWORK");
        m_font.draw_text(std::format("  {:<12}", "Interferometry:"),
                         static_cast<f32>(content_x),
                         static_cast<f32>(cursor_y),
                         1.0f,
                         kInactiveColor);
        m_font.draw_text("LOCKED",
                         static_cast<f32>(content_x + 14 * kGlyphW),
                         static_cast<f32>(cursor_y),
                         1.0f,
                         kInactiveColor);
        cursor_y += static_cast<i32>(kRowH);
        m_font.draw_text("  Build more instruments to unlock",
                         static_cast<f32>(content_x),
                         static_cast<f32>(cursor_y),
                         1.0f,
                         kInactiveColor);

        // TODO(Sprint 10+): scrollable content.
    }

    void BaseTab::on_input(const shell::InputEvent& event, const shell::ViewportRect& rect)
    {
        if (!rect.contains(event.mouse_pos))
        {
            m_hovered_location = -1;
            m_hovered_instrument = -1;
            return;
        }

        m_hovered_location = -1;
        for (i32 i = 0; i < static_cast<i32>(m_location_rows.size()); ++i)
        {
            if (m_location_rows[static_cast<std::size_t>(i)].contains(event.mouse_pos))
            {
                m_hovered_location = i;
                break;
            }
        }

        m_hovered_instrument = -1;
        for (i32 i = 0; i < static_cast<i32>(m_instrument_rows.size()); ++i)
        {
            if (m_instrument_rows[static_cast<std::size_t>(i)].contains(event.mouse_pos))
            {
                m_hovered_instrument = i;
                break;
            }
        }

        if (!event.was_click || event.click_button != shell::MouseButton::Left)
        {
            return;
        }

        if (m_hovered_instrument == 0)
        {
            if (m_hooks.on_instrument_selected)
            {
                m_hooks.on_instrument_selected("magic_instrument");
            }
            else
            {
                spdlog::info("BaseTab: instrument selected (no shell hook bound): magic_instrument");
            }
            return;
        }

        if (m_hovered_location >= 0 && m_hovered_location != m_registry.get_active_index())
        {
            m_registry.set_active(m_hovered_location);
        }
    }

    shell::TabId BaseTab::get_id() const
    {
        return shell::TabId::Base;
    }

    void BaseTab::set_shell_hooks(ShellHooks hooks)
    {
        m_hooks = std::move(hooks);
    }
}

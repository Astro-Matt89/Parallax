/// @file imaging_tab.hpp
/// @brief Interactive imaging tab for Sprint 10a total-power mode.

#pragma once

#include "core/types.hpp"
#include "imaging/image_exporter.hpp"
#include "ui/shell/tab_id.hpp"
#include "universe/celestial_object.hpp"

#include <vulkan/vulkan.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace parallax::instruments
{
    class ArrayInstrument;
}

namespace parallax::imaging
{
    class IObjectSource;
    class MultispectralImage;
}

namespace parallax::observation
{
    class SessionScheduler;
}

namespace parallax::rendering
{
    class LineRenderer;
}

namespace parallax::ui
{
    class BitmapFont;
    class Selection;
}

namespace parallax::universe { class Universe; }

namespace parallax::vulkan
{
    class Context;
    class Swapchain;
}

namespace parallax::ui::tabs
{
    /// @brief Sprint 10a imaging tab: targeting, station/band config, live preview, and integrated image display.
    class ImagingTab final : public shell::TabContent
    {
    public:
        ImagingTab(const vulkan::Context& context,
                   vulkan::Swapchain& swapchain,
                   VkRenderPass render_pass,
                   const std::filesystem::path& shader_dir,
                   universe::Universe& universe,
                   instruments::ArrayInstrument& instrument,
                   observation::SessionScheduler& scheduler,
                   Selection& selection,
                   BitmapFont& font,
                   f64& julian_date);
        ~ImagingTab() override;

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
        struct TargetState
        {
            u64 object_id = 0;
            f64 ra_rad = 0.0;
            f64 dec_rad = 0.0;
            std::string name = "No target";
        };

        enum class SessionControl
        {
            Start,
            Pause,
            Stop,
        };

        enum class SaveAction
        {
            Png,
            Fits,
        };

        enum class StretchMode
        {
            Linear,
            Log,
            Asinh,
        };

        struct UiLayout;
        class UniverseObjectSource;

        void ensure_target_initialized();
        void adopt_target_from_selection();
        void update_live_preview_objects();
        void update_session_state();
        void refresh_integrated_image();

        void handle_session_control(SessionControl action);
        void handle_save_action(SaveAction action);

        [[nodiscard]] UiLayout build_layout(const shell::ViewportRect& viewport) const;
        void render_left_column(VkCommandBuffer cmd, const shell::ViewportRect& viewport, const UiLayout& layout);
        void render_right_column(const shell::ViewportRect& viewport, const UiLayout& layout);
        void render_live_preview(const shell::ViewportRect& viewport, const shell::ViewportRect& preview_rect);
        void render_integrated_image(VkCommandBuffer cmd,
                                     const shell::ViewportRect& viewport,
                                     const shell::ViewportRect& image_rect);

        void recreate_display_texture_if_needed(u32 width_px, u32 height_px);
        void upload_display_texture(std::span<const u8> rgba8_pixels, u32 width_px, u32 height_px);

        [[nodiscard]] std::optional<std::size_t> find_display_band_plane() const;
        [[nodiscard]] std::vector<u8> build_display_pixels_rgba8() const;
        [[nodiscard]] imaging::StretchMode to_export_stretch_mode() const;
        void cycle_stretch_mode();
        void cycle_display_band();

        [[nodiscard]] bool handle_click(const shell::InputEvent& event, const UiLayout& layout);
        [[nodiscard]] bool handle_station_click(const shell::InputEvent& event, const UiLayout& layout);
        [[nodiscard]] bool handle_band_click(const shell::InputEvent& event, const UiLayout& layout);

        const vulkan::Context& m_context;
        vulkan::Swapchain& m_swapchain;
        universe::Universe& m_universe;
        instruments::ArrayInstrument& m_instrument;
        observation::SessionScheduler& m_scheduler;
        Selection& m_selection;
        BitmapFont& m_font;
        f64& m_julian_date;

        std::unique_ptr<rendering::LineRenderer> m_line_renderer;
        std::unique_ptr<UniverseObjectSource> m_object_source;

        TargetState m_target{};
        std::optional<u64> m_active_session_id;
        bool m_session_paused = false;
        f64 m_last_image_elapsed_s = -1.0;
        f64 m_last_session_snr = 0.0;

        std::vector<universe::CelestialObject> m_live_preview_objects;
        std::unique_ptr<imaging::MultispectralImage> m_integrated_image;

        u32 m_selected_display_band_index = 0;
        StretchMode m_selected_stretch_mode = StretchMode::Asinh;

        // Display texture resources.
        VkImage m_display_image = VK_NULL_HANDLE;
        VkDeviceMemory m_display_image_memory = VK_NULL_HANDLE;
        VkImageView m_display_image_view = VK_NULL_HANDLE;
        VkSampler m_display_sampler = VK_NULL_HANDLE;
        VkImageLayout m_display_image_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        u32 m_display_image_width = 0;
        u32 m_display_image_height = 0;

        VkBuffer m_display_staging_buffer = VK_NULL_HANDLE;
        VkDeviceMemory m_display_staging_memory = VK_NULL_HANDLE;
        void* m_display_staging_mapped = nullptr;
        VkDeviceSize m_display_staging_capacity = 0;

        VkDescriptorSetLayout m_texture_descriptor_set_layout = VK_NULL_HANDLE;
        VkDescriptorPool m_texture_descriptor_pool = VK_NULL_HANDLE;
        VkDescriptorSet m_texture_descriptor_set = VK_NULL_HANDLE;
        VkPipelineLayout m_texture_pipeline_layout = VK_NULL_HANDLE;
        VkPipeline m_texture_pipeline = VK_NULL_HANDLE;
        VkBuffer m_texture_vertex_buffer = VK_NULL_HANDLE;
        VkDeviceMemory m_texture_vertex_memory = VK_NULL_HANDLE;
        void* m_texture_vertex_mapped = nullptr;

        shell::ViewportRect m_last_viewport{};
        static constexpr f64 kTargetSnr = 30.0;
    };
}

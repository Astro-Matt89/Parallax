#pragma once

#include "astro/coordinates.hpp"
#include "core/input.hpp"
#include "core/types.hpp"
#include "overlay/constellations.hpp"
#include "overlay/coord_grid.hpp"
#include "overlay/horizon.hpp"
#include "rendering/dso_renderer.hpp"
#include "rendering/line_renderer.hpp"
#include "rendering/sky_background.hpp"
#include "rendering/solar_system_renderer.hpp"
#include "rendering/starfield.hpp"
#include "ui/selection.hpp"
#include "ui/shell/shell_types.hpp"
#include "ui/shell/tab_id.hpp"
#include "ui/shell/viewport_rect.hpp"
#include "universe/celestial_object.hpp"

#include <vulkan/vulkan.h>

#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace parallax::knowledge
{
    class KnowledgeDatabase;
}

namespace parallax::ui
{
    class BitmapFont;
}

namespace parallax::universe
{
    class Universe;
}

namespace parallax::vulkan
{
    class Context;
    class Swapchain;
}

namespace parallax::ui::tabs
{
    /// @brief Planetarium tab — owns the skychart rendering pipeline.
    class PlanetariumTab final : public shell::TabContent
    {
    public:
        PlanetariumTab(const vulkan::Context& context,
                       vulkan::Swapchain& swapchain,
                       VkRenderPass render_pass,
                       const std::filesystem::path& shader_dir,
                       universe::Universe& universe,
                       const knowledge::KnowledgeDatabase& knowledge,
                       BitmapFont& font,
                       f64& julian_date,
                       const astro::ObserverLocation& observer);
        ~PlanetariumTab() override = default;

        PlanetariumTab(const PlanetariumTab&)            = delete;
        PlanetariumTab& operator=(const PlanetariumTab&) = delete;
        PlanetariumTab(PlanetariumTab&&)                 = delete;
        PlanetariumTab& operator=(PlanetariumTab&&)      = delete;

        void update(f64 delta_time) override;
        void render(VkCommandBuffer cmd, const shell::ViewportRect& viewport) override;
        void on_input(const shell::InputEvent& event, const shell::ViewportRect& viewport) override;
        [[nodiscard]] shell::TabId get_id() const override;

        void set_viewport(const shell::ViewportRect& viewport) noexcept;
        void handle_keyboard(const core::Input& input);

        void set_fov(f64 fov_deg);
        [[nodiscard]] f64 get_fov_deg() const;
        void set_magnitude_limit(f32 magnitude_limit);
        void adjust_magnitude_limit(f32 delta);
        [[nodiscard]] f32 get_magnitude_limit() const;

        void toggle_constellations();
        void cycle_grid();
        void toggle_dso();
        void toggle_solar_system();
        void toggle_horizon();
        void toggle_atmosphere();
        void toggle_tracking();
        void clear_selection();
        void set_atmosphere(bool on);
        void set_bortle_scale(f32 bortle_scale);

        [[nodiscard]] bool is_atmosphere_on() const noexcept;
        [[nodiscard]] f32 get_bortle_scale() const noexcept;
        [[nodiscard]] f32 get_sun_altitude_deg() const noexcept;
        [[nodiscard]] bool constellations_visible() const noexcept;
        [[nodiscard]] overlay::GridType grid_type() const noexcept;
        [[nodiscard]] const char* grid_type_name() const;
        [[nodiscard]] bool dso_visible() const noexcept;
        [[nodiscard]] bool solar_system_visible() const noexcept;
        [[nodiscard]] bool horizon_visible() const noexcept;
        [[nodiscard]] u32 visible_star_count() const;
        [[nodiscard]] bool has_selection() const noexcept;

        [[nodiscard]] const rendering::Camera& get_camera() const noexcept;
        [[nodiscard]] const Selection& get_selection() const noexcept;
        [[nodiscard]] std::span<const universe::CelestialObject> get_frame_objects() const noexcept;

    private:
        void apply_viewport(VkCommandBuffer cmd, const shell::ViewportRect& viewport) const;
        [[nodiscard]] shell::ViewportRect current_viewport() const noexcept;

        const vulkan::Context& m_context;
        vulkan::Swapchain& m_swapchain;
        universe::Universe& m_universe;
        const knowledge::KnowledgeDatabase& m_knowledge;
        BitmapFont& m_font;
        f64& m_julian_date;
        const astro::ObserverLocation& m_observer;

        std::unique_ptr<rendering::SkyBackground> m_sky_background;
        std::unique_ptr<rendering::Starfield> m_starfield;
        std::unique_ptr<rendering::LineRenderer> m_line_renderer;
        std::unique_ptr<rendering::Camera> m_camera;

        overlay::Constellations m_constellations;
        overlay::CoordGrid m_coord_grid;
        overlay::Horizon m_horizon;
        rendering::DsoRenderer m_dso_renderer;
        rendering::SolarSystemRenderer m_solar_system_renderer;
        Selection m_selection;

        std::vector<universe::CelestialObject> m_frame_objects;

        rendering::SkyParams m_sky_params;
        bool m_atmosphere_on = true;
        f32 m_sun_altitude_deg = -90.0f;
        bool m_procedural_first_tick_logged = false;
        shell::ViewportRect m_viewport{};
    };
}

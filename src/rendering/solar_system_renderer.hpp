#pragma once

/// @file solar_system_renderer.hpp
/// @brief Skychart renderer for Solar System bodies: Sun, Moon, planets.
///
/// SKYCHART MODE — schematic rendering, not realistic:
///   Sun:     Yellow-orange filled circle, ~8px
///   Moon:    White circle, ~6px, phase shadow overlay
///   Planets: Colored circles, size from magnitude
///
/// Uses the SAME project_radec_to_screen pipeline as stars and constellations.
/// Subject to horizon culling (when atmosphere is ON).
/// Always visible regardless of magnitude limit.
/// Rendered AFTER starfield, BEFORE constellation lines.
/// Objects are selectable (clicking shows info panel).
///
/// SPRINT 06 Task 6.5

#include "astro/coordinates.hpp"
#include "astro/solar_system.hpp"
#include "core/types.hpp"
#include "rendering/camera.hpp"
#include "rendering/line_renderer.hpp"
#include "ui/font.hpp"
#include "universe/celestial_object.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace parallax::rendering
{
    /// @brief Information about a rendered Solar System body for selection picking.
    struct SolarSystemScreenObject
    {
        Vec2f screen_ndc;                   ///< Screen position in NDC [-1, 1]
        astro::EquatorialCoord equatorial;  ///< RA/Dec
        f64 alt_rad;                        ///< Altitude (radians)
        f64 az_rad;                         ///< Azimuth (radians)
        f64 distance_au;                    ///< Distance from Earth (AU)
        f32 magnitude;                      ///< Apparent visual magnitude
        f32 angular_diameter_arcsec;        ///< Angular size
        f32 phase_angle_deg;                ///< Phase angle
        f32 illumination;                   ///< Illumination fraction (0..1)
        u32 body_id;                        ///< 0=Sun, 1=Moon, 10+planet_id for planets
        std::string_view name;              ///< Display name
    };

    /// @brief Renders Sun, Moon, and planets as schematic icons in the skychart.
    ///
    /// Two usage paths:
    ///
    /// **Universe path (new):**
    ///   1. begin_frame(lines, font, viewport, atmosphere_on)  — reset per-frame state
    ///   2. add_celestial_object(screen_pos, alt_rad, az_rad, obj)  — one call per body
    ///
    /// **Legacy path (deprecated):**
    ///   1. update(bodies, moon_state, camera, observer, lst_rad, lines, font, viewport, atmosphere_on)
    ///   2. get_screen_objects() for selection picking
    class SolarSystemRenderer
    {
    public:
        SolarSystemRenderer() = default;
        ~SolarSystemRenderer() = default;

        SolarSystemRenderer(const SolarSystemRenderer&) = delete;
        SolarSystemRenderer& operator=(const SolarSystemRenderer&) = delete;
        SolarSystemRenderer(SolarSystemRenderer&&) = default;
        SolarSystemRenderer& operator=(SolarSystemRenderer&&) = default;

        // ---------------------------------------------------------------
        // Universe path (new API)
        // ---------------------------------------------------------------

        /// @brief Set up rendering context for this frame.
        ///
        /// Resets the screen objects list and stores references to the shared
        /// line renderer, font, and viewport.  Call once per frame before any
        /// add_celestial_object().
        ///
        /// @param atmosphere_on  Whether horizon culling is active.  When false
        ///                       the caller still passes pre-filtered objects that
        ///                       have already passed horizon culling (or not).
        void begin_frame(LineRenderer& lines,
                         ui::BitmapFont& font,
                         VkExtent2D viewport,
                         bool atmosphere_on);

        /// @brief Draw a single Solar System body icon + label at the given screen position.
        ///
        /// Dispatches by decoded body index from @p obj.id:
        ///   0 → Sun, 1 → Moon (uses SolarSystemData.illumination / .waxing),
        ///   2-8 → planets (styled by planet_id).
        ///
        /// Also appends to the internal screen-objects list (for selection picking).
        ///
        /// @param screen_pos  Pre-projected NDC position (from Application).
        /// @param alt_rad     Altitude of the body (radians) — stored in screen object.
        /// @param az_rad      Azimuth of the body (radians) — stored in screen object.
        /// @param obj         CelestialObject with ObjectType::SolarSystemBody.
        void add_celestial_object(Vec2f screen_pos,
                                  f64 alt_rad,
                                  f64 az_rad,
                                  const universe::CelestialObject& obj);

        // ---------------------------------------------------------------
        // Legacy path (deprecated)
        // ---------------------------------------------------------------

        /// @brief Project all Solar System bodies to screen and draw icons + labels.
        ///
        /// @deprecated Use begin_frame() + add_celestial_object() instead.
        ///
        /// @param bodies       All computed Solar System body states.
        /// @param moon_state   Extended Moon state (for phase rendering).
        /// @param camera       Current camera state.
        /// @param observer     Observer geographic location.
        /// @param lst_rad      Local sidereal time (radians).
        /// @param lines        Shared line renderer for icon geometry.
        /// @param font         Shared bitmap font for labels.
        /// @param viewport     Current viewport dimensions.
        /// @param atmosphere_on True if atmosphere/horizon culling is active.
        [[deprecated("Use begin_frame() + add_celestial_object()")]]
        void update(const astro::SolarSystem::AllBodies& bodies,
                    const astro::MoonState& moon_state,
                    const Camera& camera,
                    const astro::ObserverLocation& observer,
                    f64 lst_rad,
                    LineRenderer& lines,
                    ui::BitmapFont& font,
                    VkExtent2D viewport,
                    bool atmosphere_on);

        // ---------------------------------------------------------------
        // Common
        // ---------------------------------------------------------------

        /// @brief Set visibility of all Solar System objects.
        void set_visible(bool visible);

        /// @brief Toggle visibility.
        void toggle_visible();

        /// @brief True if Solar System rendering is enabled.
        [[nodiscard]] bool is_visible() const;

        /// @brief Number of Solar System bodies rendered in the last frame.
        [[nodiscard]] u32 get_rendered_count() const;

        /// @brief Screen objects from the last frame, for selection picking.
        ///
        /// Parallel array — each entry corresponds to a rendered body.
        /// Use body_id to distinguish: 0=Sun, 1=Moon, 10+planet_id for planets.
        [[nodiscard]] std::span<const SolarSystemScreenObject> get_screen_objects() const;

        /// @brief Body ID constants for selection system.
        ///
        /// Public so the InfoPanel and selection layer can compare against them.
        /// Planet body IDs: 10 + planet_id (e.g. 11=Mercury, 12=Venus, 14=Mars, ...)
        static constexpr u32 kBodyIdSun  = 0;
        static constexpr u32 kBodyIdMoon = 1;

    private:
        /// @brief Render the Sun icon at the given screen position.
        void draw_sun_at(Vec2f screen_pos,
                         const astro::EquatorialCoord& equatorial,
                         f64 alt_rad, f64 az_rad,
                         const universe::SolarSystemData& sd);

        /// @brief Render the Moon icon at the given screen position.
        void draw_moon_at(Vec2f screen_pos,
                          const astro::EquatorialCoord& equatorial,
                          f64 alt_rad, f64 az_rad,
                          f32 magnitude,
                          const universe::SolarSystemData& sd);

        /// @brief Render a planet icon at the given screen position.
        void draw_planet_at(Vec2f screen_pos,
                            const astro::EquatorialCoord& equatorial,
                            f64 alt_rad, f64 az_rad,
                            f32 magnitude,
                            u32 planet_id,
                            const universe::SolarSystemData& sd);

        /// @brief Render the Sun icon: yellow-orange filled circle + label.
        void render_sun(const astro::CelestialBodyState& sun,
                        const Camera& camera,
                        const astro::ObserverLocation& observer,
                        f64 lst_rad,
                        LineRenderer& lines,
                        ui::BitmapFont& font,
                        VkExtent2D viewport,
                        bool atmosphere_on);

        /// @brief Render the Moon icon: white circle + phase shadow + label.
        void render_moon(const astro::CelestialBodyState& moon,
                         const astro::MoonState& moon_state,
                         const Camera& camera,
                         const astro::ObserverLocation& observer,
                         f64 lst_rad,
                         LineRenderer& lines,
                         ui::BitmapFont& font,
                         VkExtent2D viewport,
                         bool atmosphere_on);

        /// @brief Render a planet icon: colored filled circle + label.
        void render_planet(const astro::CelestialBodyState& planet,
                           u32 planet_id,
                           const Camera& camera,
                           const astro::ObserverLocation& observer,
                           f64 lst_rad,
                           LineRenderer& lines,
                           ui::BitmapFont& font,
                           VkExtent2D viewport,
                           bool atmosphere_on);

        /// @brief Draw a filled circle approximation using concentric line circles.
        static void draw_filled_circle(Vec2f center_ndc, f32 radius_ndc,
                                       Vec4f color, LineRenderer& lines,
                                       u32 segments = 24);

        /// @brief Draw the Moon phase shadow overlay.
        ///
        /// Draws dark arcs on the unilluminated portion of the Moon disc.
        /// Uses the illumination fraction and waxing/waning to orient the shadow.
        static void draw_moon_phase(Vec2f center_ndc, f32 radius_ndc,
                                    f32 illumination, bool waxing,
                                    LineRenderer& lines);

        // -----------------------------------------------------------------
        // Pure static helpers — public for testability (no Vulkan required)
        // -----------------------------------------------------------------

    public:
        /// @brief Get the display color for a planet.
        [[nodiscard]] static Vec4f planet_color(u32 planet_id);

        /// @brief Get the display name for a planet.
        [[nodiscard]] static std::string_view planet_name(u32 planet_id);

        /// @brief Compute icon radius in NDC from apparent magnitude.
        ///
        /// Brighter objects get larger icons. Uses the same scaling concept
        /// as the starfield, but with a minimum size for visibility.
        [[nodiscard]] static f32 magnitude_to_radius_ndc(f32 magnitude);

    private:
        [[nodiscard]] static Vec2f ndc_to_pixel(Vec2f ndc, VkExtent2D viewport);

        /// @brief Try to project a body to screen, respecting horizon culling.
        ///
        /// @return Screen NDC and horizontal coords, or nullopt if culled.
        struct ProjectionResult
        {
            Vec2f screen_ndc;
            f64 alt_rad;
            f64 az_rad;
        };

        [[nodiscard]] static std::optional<ProjectionResult> project_body(
            const astro::CelestialBodyState& body,
            const Camera& camera,
            const astro::ObserverLocation& observer,
            f64 lst_rad,
            bool atmosphere_on);

        bool m_visible = true;
        u32 m_rendered_count = 0;

        /// @brief Screen objects for selection picking (rebuilt each frame).
        std::vector<SolarSystemScreenObject> m_screen_objects;

        /// @brief Transient per-frame rendering context (set by begin_frame).
        LineRenderer*   m_frame_lines       = nullptr;
        ui::BitmapFont* m_frame_font        = nullptr;
        VkExtent2D      m_frame_viewport    = {};
        bool            m_frame_atm_on      = true;

        // -----------------------------------------------------------------
        // Visual constants (skychart mode — schematic, not realistic)
        // -----------------------------------------------------------------

        /// @brief Sun icon color: warm yellow-orange.
        static constexpr Vec4f kSunColor{1.0f, 0.85f, 0.2f, 1.0f};

        /// @brief Sun icon radius in NDC (~8px at 1080p).
        static constexpr f32 kSunRadiusNdc = 0.010f;

        /// @brief Moon icon color: pale white.
        static constexpr Vec4f kMoonColor{0.9f, 0.9f, 0.95f, 1.0f};

        /// @brief Moon shadow color: dark gray (phase overlay).
        static constexpr Vec4f kMoonShadowColor{0.1f, 0.1f, 0.12f, 0.85f};

        /// @brief Moon icon radius in NDC (~6px at 1080p).
        static constexpr f32 kMoonRadiusNdc = 0.008f;

        /// @brief Sun label color (RGB for BitmapFont).
        static constexpr Vec3f kSunLabelColor{1.0f, 0.9f, 0.3f};

        /// @brief Moon label color (RGB for BitmapFont).
        static constexpr Vec3f kMoonLabelColor{0.85f, 0.85f, 0.9f};

        /// @brief Planet label color (RGB for BitmapFont).
        static constexpr Vec3f kPlanetLabelColor{0.7f, 0.9f, 0.7f};

        /// @brief Label text scale.
        static constexpr f32 kLabelScale = 1.0f;

        /// @brief Label offset from icon center in NDC (rightward and up).
        static constexpr Vec2f kLabelOffsetNdc{0.015f, 0.012f};

        /// @brief Minimum icon radius (ensures faint planets are still visible).
        static constexpr f32 kMinIconRadiusNdc = 0.004f;

        /// @brief Maximum icon radius (prevents overly large icons).
        static constexpr f32 kMaxIconRadiusNdc = 0.012f;
    };

} // namespace parallax::rendering

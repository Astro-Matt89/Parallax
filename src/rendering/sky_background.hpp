#pragma once

/// @file sky_background.hpp
/// @brief Procedural sky gradient renderer: fullscreen pass before the starfield.

#include "core/types.hpp"
#include "rendering/camera.hpp"
#include "vulkan/context.hpp"

#include <vulkan/vulkan.h>

#include <filesystem>

namespace parallax::ui::shell { struct ViewportRect; }

namespace parallax::rendering
{
    /// @brief Sky parameters controlling the appearance of the night sky.
    struct SkyParams
    {
        f32  bortle_scale       = 4.0f;   ///< Bortle dark-sky scale (1–9)
        f32  sun_altitude_deg   = -90.0f; ///< Sun altitude in degrees (< -18° = night, default safe)
        f32  sun_azimuth_deg    = 0.0f;   ///< Sun azimuth in degrees (0=N, 90=E)
        f32  moon_altitude_deg  = -90.0f; ///< Moon altitude in degrees (default: below horizon)
        f32  moon_azimuth_deg   = 0.0f;   ///< Moon azimuth in degrees
        f32  moon_illumination  = 0.0f;   ///< Moon illumination fraction (0..1)
        bool atmosphere_enabled = true;   ///< When false, sky renders as pure black
    };

    /// @brief Uniform buffer data sent to the sky background shaders each frame.
    ///
    /// Matches the `SkyUBO` layout in sky_background.frag (std140).
    ///
    /// std140 layout (48 bytes, 3 × vec4 rows):
    ///   Row 0 (bytes  0–15): camera_alt_rad, camera_az_rad, fov_rad, aspect_ratio
    ///   Row 1 (bytes 16–31): bortle_scale, sun_altitude_deg, sun_azimuth_deg, moon_altitude_deg
    ///   Row 2 (bytes 32–47): moon_azimuth_deg, moon_illumination, atmosphere_enabled (uint), _pad0
    struct SkyUniformData
    {
        // Row 0
        f32 camera_alt_rad;      ///< Camera altitude in radians
        f32 camera_az_rad;       ///< Camera azimuth in radians
        f32 fov_rad;             ///< Vertical field of view in radians
        f32 aspect_ratio;        ///< Viewport width / height
        // Row 1
        f32 bortle_scale;        ///< Bortle scale 1–9
        f32 sun_altitude_deg;    ///< Sun altitude in degrees
        f32 sun_azimuth_deg;     ///< Sun azimuth in degrees
        f32 moon_altitude_deg;   ///< Moon altitude in degrees
        // Row 2
        f32 moon_azimuth_deg;    ///< Moon azimuth in degrees
        f32 moon_illumination;   ///< Moon illumination fraction (0..1)
        u32 atmosphere_enabled;  ///< 0=disabled (pure black), 1=enabled; uint for std140 safety
        f32 _pad0 = 0.0f;        ///< Padding to complete vec4 row 2
    };

    struct SkyViewportPushConstants
    {
        Vec2f viewport_origin{0.0f, 0.0f};
        Vec2f viewport_size{1.0f, 1.0f};
    };

    /// @brief Renders a procedural sky gradient as a fullscreen pass.
    ///
    /// Uses a fullscreen triangle (3 vertices, no vertex buffer) with a uniform
    /// buffer to compute per-pixel sky color based on altitude, Bortle scale,
    /// and camera pointing. Must be rendered BEFORE the starfield (stars are
    /// additive on top).
    class SkyBackground
    {
    public:
        /// @brief Create GPU resources: uniform buffer, descriptor set, pipeline.
        /// @param context The Vulkan context (device, physical device).
        /// @param render_pass The render pass this pipeline will be used with.
        /// @param shader_dir Directory containing compiled SPIR-V files.
        /// @param extent The current viewport extent.
        SkyBackground(const vulkan::Context& context,
                      VkRenderPass render_pass,
                      const std::filesystem::path& shader_dir,
                      VkExtent2D extent);

        /// @brief Destroy all GPU resources.
        ~SkyBackground();

        SkyBackground(const SkyBackground&) = delete;
        SkyBackground& operator=(const SkyBackground&) = delete;
        SkyBackground(SkyBackground&&) = delete;
        SkyBackground& operator=(SkyBackground&&) = delete;

        /// @brief Update sky parameters and camera state for this frame.
        /// @param params Sky parameters (Bortle scale, sun/moon position).
        /// @param camera The camera (pointing direction + FOV).
        /// @param aspect_ratio Active viewport width / height.
        void update_params(const SkyParams& params, const Camera& camera, f32 aspect_ratio);

        /// @brief Record draw commands into a command buffer.
        ///
        /// Draws a fullscreen triangle with the sky gradient.
        /// Must be called inside an active render pass, BEFORE starfield rendering.
        /// @param cmd The command buffer to record into.
        /// @param viewport The active framebuffer viewport.
        void draw(VkCommandBuffer cmd, const ui::shell::ViewportRect& viewport) const;

        /// @brief Update the viewport extent (after swapchain recreation).
        /// @param extent The new swapchain extent.
        void set_extent(VkExtent2D extent);

    private:
        void create_uniform_buffer();
        void create_descriptor_set_layout();
        void create_descriptor_pool_and_set();
        void create_pipeline(VkRenderPass render_pass,
                             const std::filesystem::path& shader_dir);

        /// @brief Upload current uniform data to the mapped GPU buffer.
        void upload_uniforms();

        /// @brief Load a SPIR-V file and create a VkShaderModule.
        [[nodiscard]] VkShaderModule create_shader_module(
            const std::filesystem::path& path) const;

        const vulkan::Context& m_context;

        // GPU resources
        VkBuffer m_uniform_buffer = VK_NULL_HANDLE;
        VkDeviceMemory m_uniform_memory = VK_NULL_HANDLE;
        void* m_mapped_ptr = nullptr;   ///< Persistently mapped UBO pointer

        VkDescriptorSetLayout m_descriptor_set_layout = VK_NULL_HANDLE;
        VkDescriptorPool m_descriptor_pool = VK_NULL_HANDLE;
        VkDescriptorSet m_descriptor_set = VK_NULL_HANDLE;

        VkPipelineLayout m_pipeline_layout = VK_NULL_HANDLE;
        VkPipeline m_pipeline = VK_NULL_HANDLE;

        VkExtent2D m_extent = {0, 0};
        SkyUniformData m_uniform_data{};
    };

} // namespace parallax::rendering
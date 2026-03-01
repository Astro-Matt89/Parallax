#pragma once

/// @file sky_background.hpp
/// @brief Procedural sky gradient renderer: fullscreen pass before the starfield.

#include "core/types.hpp"
#include "rendering/camera.hpp"
#include "vulkan/context.hpp"

#include <vulkan/vulkan.h>

#include <filesystem>

namespace parallax::rendering
{
    /// @brief Sky parameters controlling the appearance of the night sky.
    struct SkyParams
    {
        f32 bortle_scale = 4.0f;        ///< Bortle dark-sky scale (1–9)
        f32 sun_altitude_deg = -18.0f;  ///< Sun altitude in degrees (< -18° = night)
        f32 moon_altitude_deg = -90.0f; ///< Moon altitude in degrees (below horizon)
        f32 moon_phase = 0.0f;          ///< Moon phase (0 = new, 1 = full)
    };

    /// @brief Uniform buffer data sent to the sky background shaders each frame.
    ///
    /// Matches the `SkyUBO` layout in sky_background.frag (std140).
    struct SkyUniformData
    {
        f32 camera_alt_rad;     ///< Camera altitude in radians
        f32 camera_az_rad;      ///< Camera azimuth in radians
        f32 fov_rad;            ///< Field of view in radians
        f32 aspect_ratio;       ///< Viewport width / height
        f32 bortle_scale;       ///< Bortle scale 1–9
        f32 sun_altitude_deg;   ///< Sun altitude in degrees
        f32 padding0 = 0.0f;
        f32 padding1 = 0.0f;
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
        void update_params(const SkyParams& params, const Camera& camera);

        /// @brief Record draw commands into a command buffer.
        ///
        /// Draws a fullscreen triangle with the sky gradient.
        /// Must be called inside an active render pass, BEFORE starfield rendering.
        /// @param cmd The command buffer to record into.
        void draw(VkCommandBuffer cmd) const;

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
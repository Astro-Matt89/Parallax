#pragma once

/// @file starfield.hpp
/// @brief Starfield renderer: CPU-side star processing + GPU storage buffer + instanced draw.

#include "astro/coordinates.hpp"
#include "catalog/star_entry.hpp"
#include "core/types.hpp"
#include "rendering/camera.hpp"
#include "vulkan/context.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace parallax::rendering
{
    /// @brief Per-instance star data uploaded to GPU each frame.
    /// Matches the vec4 layout in the starfield vertex shader.
    struct StarVertex
    {
        f32 screen_x;      ///< Normalized device coords [-1, 1]
        f32 screen_y;      ///< Normalized device coords [-1, 1]
        f32 brightness;    ///< Linear brightness (Pogson formula)
        f32 color_bv;      ///< B-V color index (converted to RGB in shader)
    };

    /// @brief Push constants for starfield rendering parameters.
    struct StarfieldPushConstants
    {
        f32 point_size_scale;   ///< Scaling factor for gl_PointSize
        f32 brightness_scale;   ///< Scaling factor for brightness
    };

    /// @brief Manages starfield rendering: CPU-side transform pipeline + GPU resources.
    ///
    /// Each frame:
    /// 1. CPU: Transform catalog stars (RA/Dec → Alt/Az → screen), compute brightness
    /// 2. CPU: Upload StarVertex array to GPU storage buffer
    /// 3. GPU: Instanced point draw with additive blending
    class Starfield
    {
    public:
        /// @brief Create GPU resources: storage buffer, descriptor set, pipeline.
        /// @param context The Vulkan context (device, physical device).
        /// @param render_pass The render pass this pipeline will be used with.
        /// @param shader_dir Directory containing compiled SPIR-V files.
        /// @param max_stars Maximum number of stars to reserve buffer space for.
        Starfield(const vulkan::Context& context,
                  VkRenderPass render_pass,
                  const std::filesystem::path& shader_dir,
                  u32 max_stars = 200000);

        /// @brief Destroy all GPU resources.
        ~Starfield();

        Starfield(const Starfield&) = delete;
        Starfield& operator=(const Starfield&) = delete;
        Starfield(Starfield&&) = delete;
        Starfield& operator=(Starfield&&) = delete;

        /// @brief Process catalog stars and upload visible ones to GPU buffer.
        ///
        /// Performs the full CPU-side transform pipeline:
        /// RA/Dec → Alt/Az (skip if below horizon) → screen projection (skip if off-screen)
        /// → magnitude→brightness (Pogson) → pack into StarVertex.
        ///
        /// @param stars The full star catalog.
        /// @param observer Observer geographic location.
        /// @param lst Local sidereal time in radians.
        /// @param camera The camera (pointing + FOV + magnitude limit).
        void update(std::span<const catalog::StarEntry> stars,
                    const astro::ObserverLocation& observer,
                    f64 lst,
                    const Camera& camera);

        /// @brief Record draw commands into a command buffer.
        /// Must be called inside an active render pass.
        /// @param cmd The command buffer to record into.
        void draw(VkCommandBuffer cmd) const;

        /// @brief Number of visible stars after the last update().
        [[nodiscard]] u32 get_visible_count() const;

        /// @brief Get the pipeline handle (for binding).
        [[nodiscard]] VkPipeline get_pipeline() const;

        /// @brief Get the pipeline layout handle.
        [[nodiscard]] VkPipelineLayout get_pipeline_layout() const;

    private:
        void create_storage_buffer(u32 max_stars);
        void create_descriptor_set_layout();
        void create_descriptor_pool_and_set();
        void create_pipeline(VkRenderPass render_pass,
                             const std::filesystem::path& shader_dir);

        /// @brief Load a SPIR-V file and create a VkShaderModule.
        [[nodiscard]] VkShaderModule create_shader_module(
            const std::filesystem::path& path) const;

        /// @brief Upload star vertex data to the mapped storage buffer.
        void upload_star_data(const std::vector<StarVertex>& vertices);

        const vulkan::Context& m_context;

        // GPU resources
        VkBuffer m_storage_buffer = VK_NULL_HANDLE;
        VkDeviceMemory m_storage_memory = VK_NULL_HANDLE;
        void* m_mapped_ptr = nullptr;         ///< Persistently mapped buffer pointer
        u32 m_buffer_capacity = 0;            ///< Max stars the buffer can hold

        VkDescriptorSetLayout m_descriptor_set_layout = VK_NULL_HANDLE;
        VkDescriptorPool m_descriptor_pool = VK_NULL_HANDLE;
        VkDescriptorSet m_descriptor_set = VK_NULL_HANDLE;

        VkPipelineLayout m_pipeline_layout = VK_NULL_HANDLE;
        VkPipeline m_pipeline = VK_NULL_HANDLE;

        // Frame state
        u32 m_visible_count = 0;
        StarfieldPushConstants m_push_constants = {6.0f, 1.5f};

        // Magnitude zero-point (Vega system: Vega ≈ mag 0)
        static constexpr f64 kMagZero = 0.0;
    };

} // namespace parallax::rendering
#pragma once

/// @file starfield.hpp
/// @brief Starfield renderer: CPU-side star processing + GPU storage buffer + instanced draw.

#include "astro/atmosphere.hpp"
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
    struct StarVertex
    {
        f32 screen_x;      ///< Normalized device coords [-1, 1]
        f32 screen_y;      ///< Normalized device coords [-1, 1]
        f32 brightness;    ///< Linear brightness (Pogson formula × extinction)
        f32 color_bv;      ///< B-V color index (reddened near horizon)
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
    /// 1. CPU: Prefilter catalog (visibility_filter) → candidate indices
    /// 2. CPU: Transform candidates (RA/Dec → Alt/Az → refraction → extinction → screen)
    /// 3. CPU: Upload StarVertex array to GPU storage buffer
    /// 4. GPU: Instanced point draw with additive blending
    class Starfield
    {
    public:
        Starfield(const vulkan::Context& context,
                  VkRenderPass render_pass,
                  const std::filesystem::path& shader_dir,
                  u32 max_stars = 200000);

        ~Starfield();

        Starfield(const Starfield&) = delete;
        Starfield& operator=(const Starfield&) = delete;
        Starfield(Starfield&&) = delete;
        Starfield& operator=(Starfield&&) = delete;

        /// @brief Process prefiltered star indices and upload visible ones to GPU.
        ///
        /// Only transforms stars at the given indices (output of VisibilityFilter).
        /// Applies atmospheric refraction, extinction, and reddening.
        ///
        /// @param stars The full star catalog.
        /// @param candidate_indices Indices into stars[] from the prefilter.
        /// @param observer Observer geographic location.
        /// @param lst Local sidereal time in radians.
        /// @param camera The camera (pointing + FOV + magnitude limit).
        /// @param atmosphere Atmospheric model for refraction and extinction.
        void update(std::span<const catalog::StarEntry> stars,
                    std::span<const u32> candidate_indices,
                    const astro::ObserverLocation& observer,
                    f64 lst,
                    const Camera& camera,
                    const astro::Atmosphere& atmosphere);

        /// @brief Record draw commands into a command buffer.
        void draw(VkCommandBuffer cmd) const;

        /// @brief Number of visible stars after the last update().
        [[nodiscard]] u32 get_visible_count() const;

        [[nodiscard]] VkPipeline get_pipeline() const;
        [[nodiscard]] VkPipelineLayout get_pipeline_layout() const;

    private:
        void create_storage_buffer(u32 max_stars);
        void create_descriptor_set_layout();
        void create_descriptor_pool_and_set();
        void create_pipeline(VkRenderPass render_pass,
                             const std::filesystem::path& shader_dir);

        [[nodiscard]] VkShaderModule create_shader_module(
            const std::filesystem::path& path) const;

        void upload_star_data(const std::vector<StarVertex>& vertices);

        const vulkan::Context& m_context;

        VkBuffer m_storage_buffer = VK_NULL_HANDLE;
        VkDeviceMemory m_storage_memory = VK_NULL_HANDLE;
        void* m_mapped_ptr = nullptr;
        u32 m_buffer_capacity = 0;

        VkDescriptorSetLayout m_descriptor_set_layout = VK_NULL_HANDLE;
        VkDescriptorPool m_descriptor_pool = VK_NULL_HANDLE;
        VkDescriptorSet m_descriptor_set = VK_NULL_HANDLE;

        VkPipelineLayout m_pipeline_layout = VK_NULL_HANDLE;
        VkPipeline m_pipeline = VK_NULL_HANDLE;

        u32 m_visible_count = 0;

        /// Push constants: tuned so that the full Hipparcos magnitude range
        /// produces visible stars on screen.
        ///
        /// Old values {6.0, 1.5} made anything fainter than mag ~2 invisible
        /// because the Pogson brightness at mag 5 is only ~0.002 and the
        /// fragment shader alpha was near-zero.
        ///
        /// New values:
        ///   point_size_scale = 8.0 → brighter stars get larger points
        ///   brightness_scale = 6.0 → mag 4 star: 0.006 × 6.0 = 0.038 → visible
        ///                            mag 6 star: 0.001 × 6.0 = 0.006 → faint dot
        StarfieldPushConstants m_push_constants = {8.0f, 6.0f};

        static constexpr f64 kMagZero = 0.0;
        static constexpr f64 kMinApparentAltRad = -0.5 * astro_constants::kDegToRad;

        /// Minimum brightness threshold after extinction.
        /// Lowered to allow faint stars near the horizon to survive culling.
        static constexpr f32 kMinBrightness = 0.0003f;

        static constexpr f32 kReddeningPerAirmass = 0.1f;
        static constexpr f32 kMinBV = -0.4f;
        static constexpr f32 kMaxBV = 2.0f;
    };

} // namespace parallax::rendering
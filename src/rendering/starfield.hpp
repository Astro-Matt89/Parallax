#pragma once

/// @file starfield.hpp
/// @brief Starfield renderer: skychart mode.
///
/// SKYCHART: No atmospheric effects on stars. Visibility controlled
/// solely by magnitude limit. Horizon culling (alt < 0°) remains.

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
        f32 mag_v;          ///< Visual magnitude (raw, for GPU normalization)
        f32 color_bv;       ///< B-V color index (real catalog data, no reddening)
    };

    /// @brief Push constants for starfield rendering parameters.
    struct StarfieldPushConstants
    {
        f32 point_size_scale;   ///< Base point size for brightest star
        f32 mag_limit;          ///< Current magnitude limit (for shader normalization)
        f32 brightest_mag;      ///< Brightest magnitude in buffer (or fixed reference)
        f32 padding;            ///< Alignment padding
    };

    /// @brief Manages starfield rendering for skychart mode.
    ///
    /// Each frame:
    /// 1. CPU: Prefilter catalog (VisibilityFilter) → candidate indices
    /// 2. CPU: Transform candidates (RA/Dec → Alt/Az → horizon cull → screen project)
    /// 3. CPU: Upload StarVertex array to GPU storage buffer
    /// 4. GPU: Instanced point draw with additive blending
    ///
    /// NO atmospheric effects: no extinction, no refraction, no reddening.
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
        /// Skychart pipeline:
        ///   RA/Dec → Alt/Az → horizon cull (alt < 0°) → screen project → upload
        ///   No refraction, no extinction, no reddening.
        void update(std::span<const catalog::StarEntry> stars,
                    std::span<const u32> candidate_indices,
                    const astro::ObserverLocation& observer,
                    f64 lst,
                    const Camera& camera);

        void draw(VkCommandBuffer cmd) const;

        [[nodiscard]] u32 get_visible_count() const;
        [[nodiscard]] VkPipeline get_pipeline() const;
        [[nodiscard]] VkPipelineLayout get_pipeline_layout() const;
        /// @brief Indices into the star catalog for each visible star this frame.
        [[nodiscard]] std::span<const u32> get_visible_indices() const;

        /// @brief Screen NDC positions for each visible star (parallel to visible indices).
        [[nodiscard]] std::span<const Vec2f> get_screen_positions() const;

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

        StarfieldPushConstants m_push_constants = {6.0f, 6.5f, -1.5f, 0.0f};

        /// Fixed reference magnitude for normalization (Sirius-class).
        static constexpr f32 kReferenceMag = -1.5f;
                /// @brief Catalog indices of visible stars this frame (for selection picking).
        std::vector<u32> m_visible_indices;

        /// @brief Screen NDC of each visible star (parallel to m_visible_indices).
        std::vector<Vec2f> m_visible_screen_positions;
    };

} // namespace parallax::rendering
#pragma once

/// @file line_renderer.hpp
/// @brief General-purpose GPU line renderer for sky overlays.
///
/// Renders LINE_LIST primitives with per-vertex RGBA color and standard
/// alpha blending. Supports individual lines, line strips, and circles.
/// Designed as the foundation for Sprint 04 overlay tasks (constellations,
/// coordinate grids, horizon markers, DSO icons).

#include "core/types.hpp"
#include "vulkan/context.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace parallax::rendering
{
    /// @brief Per-vertex data for line rendering.
    struct LineVertex
    {
        Vec2f position;  ///< Screen NDC [-1, 1]
        Vec4f color;     ///< RGBA
    };

    /// @brief General-purpose GPU line renderer for sky overlays.
    ///
    /// Usage per frame:
    ///   1. Call begin_frame() to clear the CPU vertex buffer.
    ///   2. Submit geometry via add_line(), add_line_strip(), add_circle().
    ///   3. Call render(cmd) inside an active render pass to upload and draw.
    ///
    /// Uses LINE_LIST topology so non-contiguous segments share a single draw
    /// call. No descriptor sets or push constants — pipeline layout is empty.
    class LineRenderer
    {
    public:
        LineRenderer(const vulkan::Context& context,
                     VkRenderPass render_pass,
                     const std::filesystem::path& shader_dir,
                     u32 max_vertices = 131072);

        ~LineRenderer();

        LineRenderer(const LineRenderer&) = delete;
        LineRenderer& operator=(const LineRenderer&) = delete;
        LineRenderer(LineRenderer&&) = delete;
        LineRenderer& operator=(LineRenderer&&) = delete;

        /// @brief Clear the CPU vertex buffer in preparation for a new frame.
        void begin_frame();

        /// @brief Add a single line segment (2 vertices).
        /// @param thickness Reserved; Vulkan wideLines requires a device feature.
        void add_line(Vec2f from, Vec2f to, Vec4f color, f32 thickness = 1.0f);

        /// @brief Convert N points into (N-1) LINE_LIST segments.
        /// @param thickness Reserved; Vulkan wideLines requires a device feature.
        void add_line_strip(std::span<const Vec2f> points, Vec4f color, f32 thickness = 1.0f);

        /// @brief Generate a closed circle approximation from LINE_LIST segments.
        /// @param thickness Reserved; Vulkan wideLines requires a device feature.
        void add_circle(Vec2f center, f32 radius, Vec4f color,
                        u32 segments = 32, f32 thickness = 1.0f);

        /// @brief Upload vertices to GPU and issue a single draw call.
        void render(VkCommandBuffer cmd);

        [[nodiscard]] u32 get_vertex_count() const;

    private:
        void create_vertex_buffer(u32 max_vertices);
        void create_pipeline(VkRenderPass render_pass,
                             const std::filesystem::path& shader_dir);

        [[nodiscard]] VkShaderModule create_shader_module(
            const std::filesystem::path& path) const;

        void upload_vertices();

        const vulkan::Context& m_context;

        VkBuffer m_vertex_buffer = VK_NULL_HANDLE;
        VkDeviceMemory m_vertex_memory = VK_NULL_HANDLE;
        void* m_mapped_ptr = nullptr;
        u32 m_buffer_capacity = 0;

        VkPipelineLayout m_pipeline_layout = VK_NULL_HANDLE;
        VkPipeline m_pipeline = VK_NULL_HANDLE;

        std::vector<LineVertex> m_vertices;
    };

} // namespace parallax::rendering

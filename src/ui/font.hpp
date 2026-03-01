#pragma once

/// @file font.hpp
/// @brief GPU bitmap font renderer for the retro HUD overlay.
///
/// Uses an embedded 8×16 monospace font atlas (CP437 style).
/// All text is batched into a single vertex buffer and drawn in one call.
/// Alpha blending, screen-space pixel coordinates.

#include "core/types.hpp"
#include "vulkan/context.hpp"

#include <vulkan/vulkan.h>

#include <filesystem>
#include <string>
#include <vector>

namespace parallax::ui
{
    /// @brief Per-vertex data for text quads.
    struct TextVertex
    {
        Vec2f position;   ///< Screen position in pixels
        Vec2f texcoord;   ///< UV into the font atlas texture
        Vec3f color;      ///< RGB tint color
    };

    /// @brief Push constants for the text pipeline: viewport dimensions.
    struct TextPushConstants
    {
        f32 viewport_w;   ///< Viewport width in pixels
        f32 viewport_h;   ///< Viewport height in pixels
    };

    /// @brief GPU bitmap font renderer: batched text quads, single draw call.
    ///
    /// Usage pattern each frame:
    ///   1. Call draw_text() one or more times to queue text
    ///   2. Call render() once to flush all queued text to the GPU
    ///
    /// The font atlas is generated from an embedded CP437 8×16 bitmap array
    /// at init time — no external file dependencies.
    class BitmapFont
    {
    public:
        /// @brief Create GPU resources: font atlas texture, pipeline, vertex buffer.
        /// @param context The Vulkan context.
        /// @param render_pass The render pass this pipeline will be used with.
        /// @param shader_dir Directory containing compiled SPIR-V files.
        BitmapFont(const vulkan::Context& context,
                   VkRenderPass render_pass,
                   const std::filesystem::path& shader_dir);

        /// @brief Destroy all GPU resources.
        ~BitmapFont();

        BitmapFont(const BitmapFont&) = delete;
        BitmapFont& operator=(const BitmapFont&) = delete;
        BitmapFont(BitmapFont&&) = delete;
        BitmapFont& operator=(BitmapFont&&) = delete;

        /// @brief Queue text for rendering at the given pixel position.
        ///
        /// Text is not drawn immediately — it is batched until render() is called.
        /// Coordinates are in screen pixels with origin at top-left.
        ///
        /// @param text The string to render (ASCII 32–126 supported).
        /// @param x Left edge in pixels.
        /// @param y Top edge in pixels.
        /// @param scale Scaling factor (1.0 = native 8×16, 2.0 = 16×32).
        /// @param color RGB tint color.
        void draw_text(const std::string& text, f32 x, f32 y,
                       f32 scale = 1.0f, Vec3f color = {0.0f, 1.0f, 0.0f});

        /// @brief Flush all queued text to the GPU and record draw commands.
        ///
        /// Must be called inside an active render pass, AFTER sky + starfield.
        /// Clears the internal text batch after drawing.
        ///
        /// @param cmd The command buffer to record into.
        /// @param viewport_extent The current viewport dimensions.
        void render(VkCommandBuffer cmd, VkExtent2D viewport_extent);

    private:
        void create_font_atlas();
        void create_vertex_buffer();
        void create_descriptor_set_layout();
        void create_descriptor_pool_and_set();
        void create_pipeline(VkRenderPass render_pass,
                             const std::filesystem::path& shader_dir);

        [[nodiscard]] VkShaderModule create_shader_module(
            const std::filesystem::path& path) const;

        void upload_vertices();

        const vulkan::Context& m_context;

        // Font atlas texture
        VkImage m_atlas_image = VK_NULL_HANDLE;
        VkDeviceMemory m_atlas_memory = VK_NULL_HANDLE;
        VkImageView m_atlas_view = VK_NULL_HANDLE;
        VkSampler m_atlas_sampler = VK_NULL_HANDLE;

        // Atlas layout: columns × rows grid of glyphs
        static constexpr u32 kAtlasCols = 16;  ///< Characters per row in atlas
        static constexpr u32 kAtlasRows = 6;   ///< Rows of characters in atlas
        static constexpr u32 kAtlasW = kAtlasCols * 8;   ///< 128 pixels
        static constexpr u32 kAtlasH = kAtlasRows * 16;  ///< 96 pixels

        // Vertex buffer (host-visible, dynamic)
        VkBuffer m_vertex_buffer = VK_NULL_HANDLE;
        VkDeviceMemory m_vertex_memory = VK_NULL_HANDLE;
        void* m_vertex_mapped = nullptr;
        static constexpr u32 kMaxChars = 4096;       ///< Max characters per frame
        static constexpr u32 kVertsPerChar = 6;       ///< 2 triangles per quad
        static constexpr u32 kMaxVertices = kMaxChars * kVertsPerChar;

        // Descriptor set (combined image sampler)
        VkDescriptorSetLayout m_descriptor_set_layout = VK_NULL_HANDLE;
        VkDescriptorPool m_descriptor_pool = VK_NULL_HANDLE;
        VkDescriptorSet m_descriptor_set = VK_NULL_HANDLE;

        // Pipeline
        VkPipelineLayout m_pipeline_layout = VK_NULL_HANDLE;
        VkPipeline m_pipeline = VK_NULL_HANDLE;

        // CPU-side text batch
        std::vector<TextVertex> m_batch;
    };

} // namespace parallax::ui
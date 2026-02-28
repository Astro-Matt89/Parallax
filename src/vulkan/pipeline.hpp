#pragma once

/// @file pipeline.hpp
/// @brief Render pass, graphics pipeline, and framebuffers for the test star.

#include "vulkan/context.hpp"
#include "vulkan/swapchain.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace parallax::vulkan
{
    /// @brief Manages the render pass, graphics pipeline, and per-swapchain-image framebuffers.
    ///
    /// Phase 1 test pipeline: renders a single white point at screen center.
    /// Topology is POINT_LIST, no depth buffer, no blending, dynamic viewport/scissor.
    class Pipeline
    {
    public:
        /// @brief Create render pass, load shaders, build pipeline, create framebuffers.
        /// @param context The Vulkan context (device).
        /// @param swapchain The swapchain (format, extent, image views).
        /// @param shader_dir Directory containing compiled .spv shader files.
        Pipeline(const Context& context,
                 const Swapchain& swapchain,
                 const std::filesystem::path& shader_dir);

        /// @brief Destroy framebuffers, pipeline, layout, render pass, and shader modules.
        ~Pipeline();

        Pipeline(const Pipeline&) = delete;
        Pipeline& operator=(const Pipeline&) = delete;
        Pipeline(Pipeline&&) = delete;
        Pipeline& operator=(Pipeline&&) = delete;

        /// @brief Recreate framebuffers after swapchain recreation.
        /// The render pass and pipeline remain valid if the format hasn't changed.
        /// @param swapchain The newly recreated swapchain.
        void recreate_framebuffers(const Swapchain& swapchain);

        /// @brief The render pass handle.
        [[nodiscard]] VkRenderPass get_render_pass() const;

        /// @brief The graphics pipeline handle.
        [[nodiscard]] VkPipeline get_pipeline() const;

        /// @brief The pipeline layout handle.
        [[nodiscard]] VkPipelineLayout get_layout() const;

        /// @brief Framebuffer for a given swapchain image index.
        [[nodiscard]] VkFramebuffer get_framebuffer(uint32_t image_index) const;

    private:
        void create_render_pass(VkFormat color_format);
        void create_pipeline(const std::filesystem::path& shader_dir);
        void create_framebuffers(const Swapchain& swapchain);
        void destroy_framebuffers();

        /// @brief Load a SPIR-V file and create a VkShaderModule.
        [[nodiscard]] VkShaderModule create_shader_module(const std::filesystem::path& path) const;

        const Context& m_context;

        VkRenderPass m_render_pass = VK_NULL_HANDLE;
        VkPipelineLayout m_pipeline_layout = VK_NULL_HANDLE;
        VkPipeline m_pipeline = VK_NULL_HANDLE;

        std::vector<VkFramebuffer> m_framebuffers;

        VkExtent2D m_extent = {0, 0};
    };

} // namespace parallax::vulkan
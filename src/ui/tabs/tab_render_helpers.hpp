#pragma once

#include "ui/shell/viewport_rect.hpp"

#include <vulkan/vulkan.h>

namespace parallax::ui::shell
{
    inline void apply_full_viewport_pane_scissor(VkCommandBuffer cmd,
                                                  const VkExtent2D window_extent,
                                                  const ViewportRect& pane_rect)
    {
        VkViewport vk_viewport{};
        vk_viewport.x = 0.0f;
        vk_viewport.y = 0.0f;
        vk_viewport.width = static_cast<f32>(window_extent.width);
        vk_viewport.height = static_cast<f32>(window_extent.height);
        vk_viewport.minDepth = 0.0f;
        vk_viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &vk_viewport);

        VkRect2D scissor{};
        scissor.offset = {static_cast<i32>(pane_rect.x), static_cast<i32>(pane_rect.y)};
        scissor.extent = {pane_rect.width, pane_rect.height};
        vkCmdSetScissor(cmd, 0, 1, &scissor);
    }
}

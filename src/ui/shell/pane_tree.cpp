#include "ui/shell/pane_tree.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <ranges>

namespace parallax::ui::shell
{
    namespace
    {
        struct ChildRects
        {
            ViewportRect first{};
            ViewportRect second{};
        };

        [[nodiscard]] ChildRects compute_child_rects(const Pane* pane,
                                                     const ViewportRect& available)
        {
            assert(pane != nullptr);

            const PaneKind kind = pane->get_kind();
            const f32 split_ratio = pane->get_split_ratio();

            const i32 start_x = static_cast<i32>(available.x);
            const i32 start_y = static_cast<i32>(available.y);
            const i32 end_x = static_cast<i32>(available.right());
            const i32 end_y = static_cast<i32>(available.bottom());
            const i32 half = static_cast<i32>(kSplitterThickness / 2);
            const i32 other_half = static_cast<i32>(kSplitterThickness - (kSplitterThickness / 2));

            ChildRects rects{};

            if (kind == PaneKind::HorizontalSplit)
            {
                const i32 split_x = start_x + static_cast<i32>(std::lround(static_cast<f32>(available.width) * split_ratio));

                const i32 first_end_x = split_x - half;
                const i32 second_start_x = split_x + other_half;

                const i32 first_width = std::max(0, first_end_x - start_x);
                const i32 second_width = std::max(0, end_x - second_start_x);

                rects.first = ViewportRect{
                    static_cast<u32>(start_x),
                    static_cast<u32>(start_y),
                    static_cast<u32>(first_width),
                    available.height
                };
                rects.second = ViewportRect{
                    static_cast<u32>(std::max(second_start_x, start_x)),
                    static_cast<u32>(start_y),
                    static_cast<u32>(second_width),
                    available.height
                };

                return rects;
            }

            const i32 split_y = start_y + static_cast<i32>(std::lround(static_cast<f32>(available.height) * split_ratio));

            const i32 first_end_y = split_y - half;
            const i32 second_start_y = split_y + other_half;

            const i32 first_height = std::max(0, first_end_y - start_y);
            const i32 second_height = std::max(0, end_y - second_start_y);

            rects.first = ViewportRect{
                static_cast<u32>(start_x),
                static_cast<u32>(start_y),
                available.width,
                static_cast<u32>(first_height)
            };
            rects.second = ViewportRect{
                static_cast<u32>(start_x),
                static_cast<u32>(std::max(second_start_y, start_y)),
                available.width,
                static_cast<u32>(second_height)
            };

            return rects;
        }

        [[nodiscard]] bool is_inside_axis_range(const f32 value,
                                                const u32 min_inclusive,
                                                const u32 max_exclusive)
        {
            return value >= static_cast<f32>(min_inclusive)
                && value < static_cast<f32>(max_exclusive);
        }

        [[nodiscard]] bool collapse_pass_node(Pane* node)
        {
            if (node == nullptr || node->is_leaf())
            {
                return false;
            }

            bool changed = false;

            changed = collapse_pass_node(node->get_first()) || changed;
            changed = collapse_pass_node(node->get_second()) || changed;

            Pane* first = node->get_first();
            Pane* second = node->get_second();

            if (first != nullptr
                && second != nullptr
                && (first->is_empty_leaf() || second->is_empty_leaf()))
            {
                node->merge_with_child();
                changed = true;
            }

            return changed;
        }
    }

    PaneTree::PaneTree(const TabId initial_tab)
        : m_root(Pane::make_leaf(initial_tab))
    {
    }

    Pane* PaneTree::get_root() noexcept
    {
        return m_root.get();
    }

    const Pane* PaneTree::get_root() const noexcept
    {
        return m_root.get();
    }

    void PaneTree::update_layout(const ViewportRect& root_viewport)
    {
        m_last_root_viewport = root_viewport;
        m_cached_leaves.clear();

        if (m_root != nullptr)
        {
            m_root->layout(root_viewport, m_cached_leaves);
        }
    }

    std::span<const std::pair<Pane*, ViewportRect>> PaneTree::get_leaves() const noexcept
    {
        return std::span<const std::pair<Pane*, ViewportRect>>{m_cached_leaves.data(), m_cached_leaves.size()};
    }

    Pane* PaneTree::find_pane_for_tab(const TabId tab)
    {
        return find_pane_for_tab_impl(m_root.get(), tab);
    }

    Pane* PaneTree::find_pane_for_tab_impl(Pane* node, const TabId tab)
    {
        if (node == nullptr)
        {
            return nullptr;
        }

        if (node->is_leaf())
        {
            const std::span<const TabId> tabs = node->get_tabs();
            const auto it = std::ranges::find(tabs, tab);
            return it != tabs.end() ? node : nullptr;
        }

        if (Pane* first_hit = find_pane_for_tab_impl(node->get_first(), tab); first_hit != nullptr)
        {
            return first_hit;
        }

        return find_pane_for_tab_impl(node->get_second(), tab);
    }

    Pane* PaneTree::find_leaf_at(const Vec2f screen_pos,
                                 const ViewportRect& root_viewport)
    {
        static_cast<void>(root_viewport);

        for (const auto& [pane, rect] : m_cached_leaves)
        {
            if (pane != nullptr && rect.contains(screen_pos))
            {
                return pane;
            }
        }

        return nullptr;
    }

    std::optional<SplitterHit> PaneTree::find_splitter_at(const Vec2f screen_pos,
                                                          const ViewportRect& root_viewport)
    {
        m_last_root_viewport = root_viewport;
        return find_splitter_in(m_root.get(), root_viewport, screen_pos);
    }

    std::optional<SplitterHit> PaneTree::find_splitter_in(Pane* node,
                                                          const ViewportRect& node_rect,
                                                          const Vec2f screen_pos)
    {
        if (node == nullptr || node->is_leaf())
        {
            return std::nullopt;
        }

        const ChildRects child_rects = compute_child_rects(node, node_rect);

        if (std::optional<SplitterHit> child_hit = find_splitter_in(node->get_first(), child_rects.first, screen_pos);
            child_hit.has_value())
        {
            return child_hit;
        }

        if (std::optional<SplitterHit> child_hit = find_splitter_in(node->get_second(), child_rects.second, screen_pos);
            child_hit.has_value())
        {
            return child_hit;
        }

        const ViewportRect splitter_rect = node->get_splitter_rect(node_rect);

        if (node->get_kind() == PaneKind::HorizontalSplit)
        {
            const f32 splitter_center_x = static_cast<f32>(splitter_rect.x)
                + static_cast<f32>(splitter_rect.width) * 0.5f;

            if (std::abs(screen_pos.x - splitter_center_x) <= static_cast<f32>(kSplitterHitTolerance)
                && is_inside_axis_range(screen_pos.y, node_rect.y, node_rect.bottom()))
            {
                return SplitterHit{
                    .pane = node,
                    .perpendicular_offset = screen_pos.x - splitter_center_x
                };
            }

            return std::nullopt;
        }

        const f32 splitter_center_y = static_cast<f32>(splitter_rect.y)
            + static_cast<f32>(splitter_rect.height) * 0.5f;

        if (std::abs(screen_pos.y - splitter_center_y) <= static_cast<f32>(kSplitterHitTolerance)
            && is_inside_axis_range(screen_pos.x, node_rect.x, node_rect.right()))
        {
            return SplitterHit{
                .pane = node,
                .perpendicular_offset = screen_pos.y - splitter_center_y
            };
        }

        return std::nullopt;
    }

    void PaneTree::collapse_empty_panes()
    {
        constexpr u32 kMaxCollapsePasses = 32;
        for (u32 pass = 0; pass < kMaxCollapsePasses; ++pass)
        {
            if (!collapse_pass(m_root))
            {
                return;
            }
        }

        assert(false && "PaneTree::collapse_empty_panes exceeded maximum collapse iterations");
    }

    bool PaneTree::collapse_pass(std::unique_ptr<Pane>& slot)
    {
        return collapse_pass_node(slot.get());
    }
}

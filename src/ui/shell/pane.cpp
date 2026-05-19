#include "ui/shell/pane.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace parallax::ui::shell
{
    namespace
    {
        struct ChildRects
        {
            ViewportRect first{};
            ViewportRect second{};
        };

        [[nodiscard]] ChildRects compute_child_rects(const PaneKind kind,
                                                     const f32 split_ratio,
                                                     const ViewportRect& available)
        {
            const i32 start_x = static_cast<i32>(available.x);
            const i32 start_y = static_cast<i32>(available.y);
            const i32 end_x   = static_cast<i32>(available.right());
            const i32 end_y   = static_cast<i32>(available.bottom());
            const i32 half    = static_cast<i32>(kSplitterThickness / 2);
            const i32 other_half = static_cast<i32>(kSplitterThickness - (kSplitterThickness / 2));

            ChildRects rects{};

            if (kind == PaneKind::HorizontalSplit)
            {
                const i32 split_x = start_x + static_cast<i32>(std::lround(static_cast<f32>(available.width) * split_ratio));

                const i32 first_end_x    = split_x - half;
                const i32 second_start_x = split_x + other_half;

                const i32 first_width  = std::max(0, first_end_x - start_x);
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

            const i32 first_end_y    = split_y - half;
            const i32 second_start_y = split_y + other_half;

            const i32 first_height  = std::max(0, first_end_y - start_y);
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
    }

    std::unique_ptr<Pane> Pane::make_leaf(const TabId initial_tab)
    {
        auto pane = std::make_unique<Pane>(PrivateTag{});
        pane->m_kind = PaneKind::Leaf;
        pane->m_tabs.push_back(initial_tab);
        pane->m_active_tab_index = 0;
        return pane;
    }

    std::unique_ptr<Pane> Pane::make_split(PaneKind direction,
                                           std::unique_ptr<Pane> first,
                                           std::unique_ptr<Pane> second,
                                           const f32 split_ratio)
    {
        assert(direction == PaneKind::HorizontalSplit || direction == PaneKind::VerticalSplit);
        assert(first != nullptr);
        assert(second != nullptr);

        auto pane = std::make_unique<Pane>(PrivateTag{});
        pane->m_kind = direction;
        pane->m_first = std::move(first);
        pane->m_second = std::move(second);
        pane->m_split_ratio = std::clamp(split_ratio, kMinSplitRatio, kMaxSplitRatio);
        return pane;
    }

    void Pane::add_tab(const TabId tab)
    {
        assert(is_leaf());

        const auto it = std::find(m_tabs.begin(), m_tabs.end(), tab);
        if (it != m_tabs.end())
        {
            m_active_tab_index = static_cast<u32>(std::distance(m_tabs.begin(), it));
            return;
        }

        m_tabs.push_back(tab);
        m_active_tab_index = static_cast<u32>(m_tabs.size() - 1);
    }

    void Pane::remove_tab(const TabId tab)
    {
        assert(is_leaf());

        const auto it = std::find(m_tabs.begin(), m_tabs.end(), tab);
        if (it == m_tabs.end())
        {
            return;
        }

        const u32 removed_index = static_cast<u32>(std::distance(m_tabs.begin(), it));
        const u32 old_active_index = m_active_tab_index;

        m_tabs.erase(it);

        if (m_tabs.empty())
        {
            m_active_tab_index = 0;
            return;
        }

        u32 adjusted_active_index = old_active_index;
        if (removed_index < old_active_index)
        {
            adjusted_active_index -= 1;
        }

        const u32 max_valid_index = static_cast<u32>(m_tabs.size() - 1);
        m_active_tab_index = std::min(adjusted_active_index, max_valid_index);
    }

    void Pane::set_active_tab(const TabId tab)
    {
        assert(is_leaf());

        const auto it = std::find(m_tabs.begin(), m_tabs.end(), tab);
        if (it == m_tabs.end())
        {
            return;
        }

        m_active_tab_index = static_cast<u32>(std::distance(m_tabs.begin(), it));
    }

    TabId Pane::get_active_tab() const
    {
        assert(is_leaf());
        assert(!m_tabs.empty());
        return m_tabs[m_active_tab_index];
    }

    void Pane::split(const PaneKind direction, const TabId new_pane_tab, const bool new_pane_first)
    {
        assert(is_leaf());
        assert(direction == PaneKind::HorizontalSplit || direction == PaneKind::VerticalSplit);

        auto existing_leaf = std::make_unique<Pane>(PrivateTag{});
        existing_leaf->m_kind = PaneKind::Leaf;
        existing_leaf->m_tabs = std::move(m_tabs);
        existing_leaf->m_active_tab_index = m_active_tab_index;

        auto new_leaf = make_leaf(new_pane_tab);

        m_kind = direction;
        m_tabs.clear();
        m_active_tab_index = 0;
        m_split_ratio = 0.5f;

        if (new_pane_first)
        {
            m_first = std::move(new_leaf);
            m_second = std::move(existing_leaf);
        }
        else
        {
            m_first = std::move(existing_leaf);
            m_second = std::move(new_leaf);
        }
    }

    void Pane::merge_with_child()
    {
        assert(is_split());
        assert(m_first != nullptr);
        assert(m_second != nullptr);

        if (m_first->is_empty_leaf() && m_second->is_empty_leaf())
        {
            m_kind = PaneKind::Leaf;
            m_tabs.clear();
            m_active_tab_index = 0;
            m_first.reset();
            m_second.reset();
            m_split_ratio = 0.5f;
            return;
        }

        const bool first_empty_leaf = m_first->is_empty_leaf();
        const bool second_empty_leaf = m_second->is_empty_leaf();
        assert(first_empty_leaf || second_empty_leaf);
        Pane* survivor = first_empty_leaf ? m_second.get() : m_first.get();

        m_kind = survivor->m_kind;
        m_tabs = std::move(survivor->m_tabs);
        m_active_tab_index = survivor->m_active_tab_index;
        m_split_ratio = survivor->m_split_ratio;

        auto survivor_first = std::move(survivor->m_first);
        auto survivor_second = std::move(survivor->m_second);

        m_first = std::move(survivor_first);
        m_second = std::move(survivor_second);
    }

    void Pane::set_split_ratio(const f32 ratio) noexcept
    {
        if (is_leaf())
        {
            return;
        }

        m_split_ratio = std::clamp(ratio, kMinSplitRatio, kMaxSplitRatio);
    }

    void Pane::layout(const ViewportRect& available,
                      std::vector<std::pair<Pane*, ViewportRect>>& leaves_out)
    {
        if (is_leaf())
        {
            leaves_out.emplace_back(this, available);
            return;
        }

        assert(m_first != nullptr);
        assert(m_second != nullptr);

        const ChildRects child_rects = compute_child_rects(m_kind, m_split_ratio, available);
        m_first->layout(child_rects.first, leaves_out);
        m_second->layout(child_rects.second, leaves_out);
    }

    ViewportRect Pane::get_splitter_rect(const ViewportRect& available) const noexcept
    {
        assert(is_split());

        const i32 start_x = static_cast<i32>(available.x);
        const i32 start_y = static_cast<i32>(available.y);
        const i32 end_x = static_cast<i32>(available.right());
        const i32 end_y = static_cast<i32>(available.bottom());
        const i32 half = static_cast<i32>(kSplitterThickness / 2);

        if (m_kind == PaneKind::HorizontalSplit)
        {
            const i32 split_x = start_x + static_cast<i32>(std::lround(static_cast<f32>(available.width) * m_split_ratio));
            const i32 splitter_x = split_x - half;
            const i32 clipped_x = std::clamp(splitter_x, start_x, end_x);
            const i32 clipped_width = std::max(0, std::min(static_cast<i32>(kSplitterThickness), end_x - clipped_x));

            return ViewportRect{
                static_cast<u32>(clipped_x),
                available.y,
                static_cast<u32>(clipped_width),
                available.height
            };
        }

        const i32 split_y = start_y + static_cast<i32>(std::lround(static_cast<f32>(available.height) * m_split_ratio));
        const i32 splitter_y = split_y - half;
        const i32 clipped_y = std::clamp(splitter_y, start_y, end_y);
        const i32 clipped_height = std::max(0, std::min(static_cast<i32>(kSplitterThickness), end_y - clipped_y));

        return ViewportRect{
            available.x,
            static_cast<u32>(clipped_y),
            available.width,
            static_cast<u32>(clipped_height)
        };
    }
}

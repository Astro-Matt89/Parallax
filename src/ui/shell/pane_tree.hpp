#pragma once

#include "core/types.hpp"
#include "ui/shell/pane.hpp"
#include "ui/shell/shell_types.hpp"
#include "ui/shell/tab_id.hpp"
#include "ui/shell/viewport_rect.hpp"

#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace parallax::ui::shell
{
    /// @brief Result of a splitter hit-test.
    struct SplitterHit
    {
        Pane* pane = nullptr;  ///< The split pane whose splitter was hit.
        f32 perpendicular_offset = 0.0f;
        ///< Signed offset (pixels) of the cursor from the splitter centre,
        ///< measured along the axis the splitter would move when dragged.
        ///< Positive = below/right of the centre.
    };

    class PaneTree
    {
    public:
        /// Constructs a tree whose root is a single leaf with @p initial_tab
        /// (the brief specifies PLANETARIUM as the default starting tab).
        explicit PaneTree(TabId initial_tab);

        PaneTree(const PaneTree&) = delete;
        PaneTree& operator=(const PaneTree&) = delete;
        PaneTree(PaneTree&&) noexcept = default;
        PaneTree& operator=(PaneTree&&) noexcept = default;

        [[nodiscard]] Pane* get_root() noexcept;
        [[nodiscard]] const Pane* get_root() const noexcept;

        /// Recompute and cache leaf viewports for the current frame.
        void update_layout(const ViewportRect& root_viewport);

        /// Read-only view of the cached `(leaf, viewport)` pairs.
        [[nodiscard]] std::span<const std::pair<Pane*, ViewportRect>> get_leaves() const noexcept;

        /// First leaf in DFS order containing @p tab in its tab list, or nullptr.
        [[nodiscard]] Pane* find_pane_for_tab(TabId tab);

        /// Cached-layout-based hit-test. Returns the leaf whose rect contains
        /// @p screen_pos (in framebuffer pixels), or nullptr if none matches.
        /// `root_viewport` is accepted for API symmetry with the brief but
        /// not actually used because the cached layout already encodes it;
        /// pass the same value that was given to @ref update_layout.
        [[nodiscard]] Pane* find_leaf_at(Vec2f screen_pos,
                                         const ViewportRect& root_viewport);

        /// Search all split panes for one whose splitter is within
        /// kSplitterHitTolerance pixels of @p screen_pos.
        [[nodiscard]] std::optional<SplitterHit> find_splitter_at(
            Vec2f screen_pos,
            const ViewportRect& root_viewport);

        /// Walk the tree top-down and collapse any split whose either child
        /// is an empty leaf, by calling Pane::merge_with_child(). Repeated
        /// until the tree stabilises. After this, the root may be an empty
        /// leaf; callers should re-seed it (the brief never lets the user
        /// reach that state, but be defensive).
        void collapse_empty_panes();

    private:
        // Recursive helpers.
        Pane* find_pane_for_tab_impl(Pane* node, TabId tab);
        std::optional<SplitterHit> find_splitter_in(
            Pane* node, const ViewportRect& node_rect, Vec2f screen_pos);
        // Returns true if any merge happened, so caller can re-iterate.
        bool collapse_pass(std::unique_ptr<Pane>& slot);

        std::unique_ptr<Pane> m_root;
        std::vector<std::pair<Pane*, ViewportRect>> m_cached_leaves;
        ViewportRect m_last_root_viewport{};
    };
}

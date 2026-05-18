/// @file pane.hpp
/// @brief Recursive pane tree node — either a tab-bearing leaf or a binary split.
///
/// SPRINT 09 Task 9.2 — Shell: Pane Tree.
///
/// A `Pane` is the unit of the central area's layout. It is either a
/// `Leaf` that hosts one or more tabs (one of which is active and rendered)
/// or an internal node that splits its rect between two child panes
/// (`HorizontalSplit` → children left/right, `VerticalSplit` → top/bottom).
///
/// Construction goes through static factories so callers don't have to
/// reason about internal invariants:
///   - @ref Pane::make_leaf — terminal node with a single starting tab.
///   - @ref Pane::make_split — binary split with two existing child panes.
///
/// Mutating operations:
///   - @ref add_tab / @ref remove_tab / @ref set_active_tab on leaves.
///   - @ref split — turns a leaf into a split, putting its existing tabs
///     on one side and a new leaf with @p new_pane_tab on the other.
///   - @ref merge_with_child — replaces a split whose other child is the
///     valid one with that child (used when a leaf becomes empty).
///   - @ref set_split_ratio — drag handler; clamps to [0.1, 0.9].
///
/// Layout:
///   - @ref layout walks the tree, assigning ViewportRects to every leaf
///     descendant. The output is appended to the supplied vector so a
///     PaneTree (Task 9.2) can cache it for input routing.
///
/// Splitter visuals (constants exposed for use by PaneTree / Shell):
///   - @ref kSplitterThickness — 4 px visible width of the splitter line.
///   - @ref kSplitterHitTolerance — 6 px hover/grab tolerance around the line.

#pragma once

#include "core/types.hpp"
#include "ui/shell/shell_types.hpp"
#include "ui/shell/tab_id.hpp"
#include "ui/shell/viewport_rect.hpp"

#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace parallax::ui::shell
{
    // ------------------------------------------------------------------
    // Splitter constants
    // ------------------------------------------------------------------

    /// Visible thickness, in pixels, of a splitter line between two panes.
    /// Children are inset by half this amount on the splitter side so that
    /// they do not overlap the line.
    inline constexpr u32 kSplitterThickness = 4;

    /// Distance, in pixels, within which the cursor is considered to be
    /// "over" a splitter for hover/drag-grab detection. Matches the brief
    /// ("Hover state: brighter green when mouse is within 6px of splitter").
    inline constexpr u32 kSplitterHitTolerance = 6;

    /// Lower bound on the split ratio. Prevents collapsing one side to nothing.
    inline constexpr f32 kMinSplitRatio = 0.1f;

    /// Upper bound on the split ratio.
    inline constexpr f32 kMaxSplitRatio = 0.9f;

    // ------------------------------------------------------------------
    // Pane
    // ------------------------------------------------------------------

    class Pane
    {
    public:
        /// Passkey used by the public factories to construct a Pane via
        /// `std::make_unique`. Direct construction is disallowed because the
        /// invariants depend on the factory used (Leaf vs Split).
        struct PrivateTag
        {
            explicit PrivateTag() = default;
        };

        explicit Pane(PrivateTag) noexcept {}

        Pane(const Pane&)            = delete;
        Pane& operator=(const Pane&) = delete;
        Pane(Pane&&)                 = delete;
        Pane& operator=(Pane&&)      = delete;

        // --------------------------------------------------------------
        // Construction
        // --------------------------------------------------------------

        /// @brief Create a leaf pane holding a single starting tab.
        [[nodiscard]] static std::unique_ptr<Pane> make_leaf(TabId initial_tab);

        /// @brief Create a split pane with two existing child panes.
        /// @param direction Must be HorizontalSplit or VerticalSplit.
        /// @param first     Left/top child (ownership transferred).
        /// @param second    Right/bottom child (ownership transferred).
        /// @param split_ratio Fraction of the parent's primary axis given to
        ///        @p first; clamped to [kMinSplitRatio, kMaxSplitRatio].
        [[nodiscard]] static std::unique_ptr<Pane> make_split(
            PaneKind direction,
            std::unique_ptr<Pane> first,
            std::unique_ptr<Pane> second,
            f32 split_ratio = 0.5f);

        // --------------------------------------------------------------
        // Identity / kind
        // --------------------------------------------------------------

        [[nodiscard]] PaneKind get_kind() const noexcept { return m_kind; }
        [[nodiscard]] bool is_leaf()  const noexcept { return m_kind == PaneKind::Leaf; }
        [[nodiscard]] bool is_split() const noexcept { return !is_leaf(); }

        // --------------------------------------------------------------
        // Tab operations (valid on leaves only)
        // --------------------------------------------------------------

        /// Append @p tab to this leaf's tab list. No-op if already present;
        /// in that case the existing instance is made active.
        void add_tab(TabId tab);

        /// Remove @p tab from this leaf. No-op if not present. Updates the
        /// active index to remain valid; if the last tab is removed the
        /// leaf becomes empty (see PaneTree for collapse handling).
        void remove_tab(TabId tab);

        /// Make @p tab the active tab. No-op if @p tab is not in this leaf.
        void set_active_tab(TabId tab);

        /// @return The currently active tab id. Caller must ensure the leaf
        ///         is not empty; otherwise behaviour is undefined.
        [[nodiscard]] TabId get_active_tab() const;

        /// @return Index of the active tab in @ref get_tabs.
        [[nodiscard]] u32 get_active_tab_index() const noexcept { return m_active_tab_index; }

        /// @return Read-only view of this leaf's tab list, in order.
        [[nodiscard]] std::span<const TabId> get_tabs() const noexcept
        {
            return std::span<const TabId>{m_tabs.data(), m_tabs.size()};
        }

        [[nodiscard]] bool is_empty_leaf() const noexcept
        {
            return m_kind == PaneKind::Leaf && m_tabs.empty();
        }

        // --------------------------------------------------------------
        // Split operations
        // --------------------------------------------------------------

        /// @brief Convert this leaf into a split.
        ///
        /// The existing tabs are moved into a new leaf, and a second leaf
        /// containing @p new_pane_tab is created. The two children are
        /// arranged according to @p new_pane_first:
        ///   - true  → new leaf is @ref get_first, existing is @ref get_second.
        ///   - false → existing is first, new is second.
        ///
        /// @pre This pane must currently be a leaf.
        void split(PaneKind direction, TabId new_pane_tab, bool new_pane_first);

        /// @brief Replace this split with its single non-empty child.
        ///
        /// Used after a tab close leaves one child as an empty leaf. The
        /// surviving child's contents (kind, tabs OR sub-panes, split ratio)
        /// are moved into @c *this; this pane keeps its identity (so any
        /// caller-held `Pane*` remains valid) but takes on the survivor's
        /// role.
        ///
        /// @pre This pane must be a split with exactly one empty-leaf child.
        ///      If both children are empty leaves this pane becomes an empty
        ///      leaf itself, which the caller (PaneTree) should then collapse
        ///      one level up.
        void merge_with_child();

        // --------------------------------------------------------------
        // Split geometry / dragging
        // --------------------------------------------------------------

        /// Set the split ratio, clamped to [kMinSplitRatio, kMaxSplitRatio].
        /// No-op on leaves.
        void set_split_ratio(f32 ratio) noexcept;

        [[nodiscard]] f32 get_split_ratio() const noexcept { return m_split_ratio; }

        /// @return First child (left/top); nullptr on leaves.
        [[nodiscard]] Pane* get_first()  const noexcept { return m_first.get(); }
        /// @return Second child (right/bottom); nullptr on leaves.
        [[nodiscard]] Pane* get_second() const noexcept { return m_second.get(); }

        // --------------------------------------------------------------
        // Layout
        // --------------------------------------------------------------

        /// @brief Compute viewport rects for every leaf descendant.
        ///
        /// Walks the subtree rooted at @c *this. For each leaf encountered
        /// (including @c *this itself if a leaf), appends a `(Pane*, rect)`
        /// pair to @p leaves_out, where the rect is the leaf's slice of
        /// @p available.
        ///
        /// Splitter inset: children are shrunk by `kSplitterThickness / 2`
        /// pixels on the splitter side so the splitter line can be drawn
        /// between them without overlapping content.
        void layout(const ViewportRect& available,
                    std::vector<std::pair<Pane*, ViewportRect>>& leaves_out);

        /// @brief Compute the on-screen geometry of this pane's splitter line.
        /// @pre This pane must be a split.
        /// @param available The rect this split occupies (same value that
        ///        would be passed to @ref layout for this pane).
        /// @return The 4-pixel-thick rect occupied by the splitter line.
        [[nodiscard]] ViewportRect get_splitter_rect(const ViewportRect& available) const noexcept;

    private:
        // ----- Common -----
        PaneKind m_kind = PaneKind::Leaf;

        // ----- Leaf state -----
        std::vector<TabId> m_tabs;
        u32                m_active_tab_index = 0;

        // ----- Split state -----
        std::unique_ptr<Pane> m_first;
        std::unique_ptr<Pane> m_second;
        f32                   m_split_ratio = 0.5f;
    };
}

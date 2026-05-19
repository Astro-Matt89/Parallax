#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "ui/shell/pane_tree.hpp"

namespace parallax::ui::shell
{
    TEST_CASE("Pane split ratio clamps to allowed range")
    {
        auto pane = Pane::make_split(
            PaneKind::HorizontalSplit,
            Pane::make_leaf(TabId::Planetarium),
            Pane::make_leaf(TabId::Archive),
            0.5f);

        pane->set_split_ratio(-1.0f);
        CHECK(pane->get_split_ratio() == doctest::Approx(kMinSplitRatio));

        pane->set_split_ratio(2.0f);
        CHECK(pane->get_split_ratio() == doctest::Approx(kMaxSplitRatio));
    }

    TEST_CASE("PaneTree layout emits non-overlapping leaf viewports")
    {
        PaneTree tree(TabId::Planetarium);
        Pane* root = tree.get_root();
        REQUIRE(root != nullptr);

        root->split(PaneKind::HorizontalSplit, TabId::Archive, false);

        Pane* archive_pane = tree.find_pane_for_tab(TabId::Archive);
        REQUIRE(archive_pane != nullptr);
        archive_pane->split(PaneKind::VerticalSplit, TabId::Encyclopedia, false);

        const ViewportRect viewport{.x = 0, .y = 0, .width = 1000, .height = 600};
        tree.update_layout(viewport);

        const auto leaves = tree.get_leaves();
        REQUIRE(leaves.size() == 3);

        for (size_t i = 0; i < leaves.size(); ++i)
        {
            for (size_t j = i + 1; j < leaves.size(); ++j)
            {
                const auto& a = leaves[i].second;
                const auto& b = leaves[j].second;

                const bool intersects = a.x < b.right()
                    && b.x < a.right()
                    && a.y < b.bottom()
                    && b.y < a.bottom();
                CHECK_FALSE(intersects);
            }
        }

        for (const auto& [pane, rect] : leaves)
        {
            const Vec2f center = rect.get_center();
            CHECK(tree.find_leaf_at(center, viewport) == pane);
        }
    }

    TEST_CASE("PaneTree splitter hit-test prefers innermost split")
    {
        PaneTree tree(TabId::Planetarium);
        Pane* root = tree.get_root();
        REQUIRE(root != nullptr);

        root->split(PaneKind::VerticalSplit, TabId::Archive, false);

        Pane* top_pane = tree.find_pane_for_tab(TabId::Planetarium);
        REQUIRE(top_pane != nullptr);
        top_pane->split(PaneKind::HorizontalSplit, TabId::Encyclopedia, false);

        const ViewportRect viewport{.x = 0, .y = 0, .width = 1000, .height = 600};
        const std::optional<SplitterHit> hit = tree.find_splitter_at(Vec2f{500.0f, 295.0f}, viewport);

        REQUIRE(hit.has_value());
        CHECK(hit->pane == top_pane);
        CHECK(hit->perpendicular_offset == doctest::Approx(0.0f));
    }

    TEST_CASE("PaneTree collapse_empty_panes merges split with empty leaf child")
    {
        PaneTree tree(TabId::Planetarium);
        Pane* root = tree.get_root();
        REQUIRE(root != nullptr);

        root->split(PaneKind::HorizontalSplit, TabId::Archive, false);

        Pane* archive_pane = tree.find_pane_for_tab(TabId::Archive);
        REQUIRE(archive_pane != nullptr);
        archive_pane->remove_tab(TabId::Archive);

        tree.collapse_empty_panes();

        Pane* new_root = tree.get_root();
        REQUIRE(new_root != nullptr);
        CHECK(new_root->is_leaf());
        REQUIRE(new_root->get_tabs().size() == 1);
        CHECK(new_root->get_tabs()[0] == TabId::Planetarium);
    }
}

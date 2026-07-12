// SPDX-License-Identifier: Apache-2.0
// roam-24 (I-4.4): the pure traversal controller. Covers the §6.9 matrix —
// derived-MRU distinct-first-occurrence, order within a gesture, Forward
// inverts Back over the frozen order, active-close anchor positioning (first
// Back after closing the active tab lands on the MRU-previous tab, never a
// no-op), enablement edges, all-closed no-op, reachability (reopen on/off)
// filtering, empty/single, settle, and re-entrant BeginGesture. Identities are
// ABSTRACT tokens ("A", "B", …) — never URLs — so "distinct tab" is what is
// pinned.

#include "roamux/browser/tab_visit/tab_traversal_controller.h"

#include <optional>
#include <string>
#include <vector>

#include "base/containers/flat_set.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace roamux::tab_visit {
namespace {

using Journal = std::vector<TabIdentity>;
using Reachable = base::flat_set<TabIdentity>;

TEST(DeriveMruOrderTest, DistinctFirstOccurrenceNewestFirst) {
  // Oldest->newest: A,B,C,B,D. Newest->oldest first occurrence: D,B,C,A.
  EXPECT_EQ(DeriveMruOrder({"A", "B", "C", "B", "D"}),
            Journal({"D", "B", "C", "A"}));
}

TEST(DeriveMruOrderTest, EmptyAndSingle) {
  EXPECT_TRUE(DeriveMruOrder({}).empty());
  EXPECT_EQ(DeriveMruOrder({"A"}), Journal({"A"}));
  EXPECT_EQ(DeriveMruOrder({"A", "A", "A"}), Journal({"A"}));
}

TEST(TabTraversalControllerTest, OrderWithinGesture) {
  TabTraversalController c;
  c.BeginGesture({"A", "B", "C", "D"}, Reachable({"A", "B", "C", "D"}));
  // MRU = [D,C,B,A], all landable, cursor at D (index 0).
  EXPECT_EQ(c.Back(), std::optional<TabIdentity>("C"));
  EXPECT_EQ(c.Back(), std::optional<TabIdentity>("B"));
  EXPECT_EQ(c.Back(), std::optional<TabIdentity>("A"));
  EXPECT_EQ(c.Back(), std::nullopt);  // Disabled at the oldest landing.
}

TEST(TabTraversalControllerTest, ForwardInvertsBack) {
  TabTraversalController c;
  c.BeginGesture({"A", "B", "C", "D"}, Reachable({"A", "B", "C", "D"}));
  ASSERT_EQ(c.Back(), std::optional<TabIdentity>("C"));
  ASSERT_EQ(c.Back(), std::optional<TabIdentity>("B"));
  ASSERT_EQ(c.Back(), std::optional<TabIdentity>("A"));
  // Forward retraces the exact inverse path over the frozen order.
  EXPECT_EQ(c.Forward(), std::optional<TabIdentity>("B"));
  EXPECT_EQ(c.Forward(), std::optional<TabIdentity>("C"));
  EXPECT_EQ(c.Forward(), std::optional<TabIdentity>("D"));
  EXPECT_EQ(c.Forward(), std::nullopt);  // Back at the anchor; nothing newer.
}

TEST(TabTraversalControllerTest, ActiveCloseAnchorPositioning) {
  // Plan §6.9 line 302: visit order A->B->C->D (D active, journal tail); close
  // D so it is unreachable. MRU = [D,C,B,A]; anchor D at index 0 is the origin,
  // not a landing. The first Back must land on C (the MRU-previous tab), never
  // a no-op, independent of any tab-strip auto-successor.
  TabTraversalController c;
  c.BeginGesture({"A", "B", "C", "D"}, Reachable({"A", "B", "C"}));
  EXPECT_EQ(c.Current(), std::nullopt);  // On the closed anchor origin.
  EXPECT_TRUE(c.CanGoBack());
  EXPECT_FALSE(c.CanGoForward());  // Nothing newer than the origin.
  EXPECT_EQ(c.Back(), std::optional<TabIdentity>("C"));
  EXPECT_EQ(c.Current(), std::optional<TabIdentity>("C"));
}

TEST(TabTraversalControllerTest, EnablementEdges) {
  TabTraversalController c;
  c.BeginGesture({"A", "B", "C"}, Reachable({"A", "B", "C"}));
  // At the anchor (C), Forward has nothing newer; Back does.
  EXPECT_FALSE(c.CanGoForward());
  EXPECT_TRUE(c.CanGoBack());
  ASSERT_EQ(c.Back(), std::optional<TabIdentity>("B"));
  ASSERT_EQ(c.Back(), std::optional<TabIdentity>("A"));
  // At the oldest landing, Back is disabled; Forward is enabled.
  EXPECT_FALSE(c.CanGoBack());
  EXPECT_TRUE(c.CanGoForward());
}

TEST(TabTraversalControllerTest, AllClosedIsANoOp) {
  TabTraversalController c;
  c.BeginGesture({"A", "B"}, Reachable({}));  // Both closed / unreachable.
  EXPECT_FALSE(c.CanGoBack());
  EXPECT_FALSE(c.CanGoForward());
  EXPECT_EQ(c.Back(), std::nullopt);
  EXPECT_EQ(c.Current(), std::nullopt);
  EXPECT_EQ(c.Settle(), std::nullopt);
}

TEST(TabTraversalControllerTest, ReachabilityFilteringChangesLanding) {
  // Same journal, different reachable sets => different first landing, proving
  // reachability filtering (reopen_closed off vs on).
  {
    TabTraversalController c;
    c.BeginGesture({"A", "B", "C", "D"}, Reachable({"A", "B", "C", "D"}));
    EXPECT_EQ(c.Back(), std::optional<TabIdentity>("C"));  // Next reachable.
  }
  {
    TabTraversalController c;
    c.BeginGesture({"A", "B", "C", "D"}, Reachable({"A", "D"}));
    // MRU [D,C,B,A]; C and B unreachable -> first Back skips to A.
    EXPECT_EQ(c.Back(), std::optional<TabIdentity>("A"));
  }
}

TEST(TabTraversalControllerTest, EmptyJournal) {
  TabTraversalController c;
  c.BeginGesture({}, Reachable({}));
  EXPECT_FALSE(c.CanGoBack());
  EXPECT_FALSE(c.CanGoForward());
  EXPECT_EQ(c.Back(), std::nullopt);
  EXPECT_EQ(c.Forward(), std::nullopt);
  EXPECT_EQ(c.Current(), std::nullopt);
  EXPECT_EQ(c.Settle(), std::nullopt);
}

TEST(TabTraversalControllerTest, SingleReachableTabHasNoTraversal) {
  TabTraversalController c;
  c.BeginGesture({"A"}, Reachable({"A"}));
  EXPECT_FALSE(c.CanGoBack());
  EXPECT_FALSE(c.CanGoForward());
  EXPECT_EQ(c.Current(), std::optional<TabIdentity>("A"));
  EXPECT_EQ(c.Settle(), std::optional<TabIdentity>("A"));
}

TEST(TabTraversalControllerTest, SettleReturnsCurrentLandingAndClears) {
  TabTraversalController c;
  c.BeginGesture({"A", "B", "C"}, Reachable({"A", "B", "C"}));
  ASSERT_EQ(c.Back(), std::optional<TabIdentity>("B"));
  EXPECT_EQ(c.Settle(), std::optional<TabIdentity>("B"));
  EXPECT_FALSE(c.gesture_active());
}

TEST(TabTraversalControllerTest, ReentrantBeginGestureIsIgnored) {
  TabTraversalController c;
  c.BeginGesture({"A", "B", "C", "D"}, Reachable({"A", "B", "C", "D"}));
  ASSERT_EQ(c.Back(), std::optional<TabIdentity>("C"));  // cursor at index 1.

  // A second BeginGesture while active must be IGNORED — the frozen order and
  // cursor are protected.
  c.BeginGesture({"X", "Y"}, Reachable({"X", "Y"}));
  EXPECT_EQ(c.Current(), std::optional<TabIdentity>("C"));
  EXPECT_EQ(c.Back(), std::optional<TabIdentity>("B"));  // Still the old order.

  // After Settle, a fresh gesture with the new journal DOES take effect.
  EXPECT_EQ(c.Settle(), std::optional<TabIdentity>("B"));
  c.BeginGesture({"X", "Y"}, Reachable({"X", "Y"}));
  EXPECT_EQ(c.Back(), std::optional<TabIdentity>("X"));  // MRU [Y,X].
}

TEST(CanBeginBackTraversalTest, PreGestureEnablement) {
  EXPECT_TRUE(
      CanBeginBackTraversal({"A", "B", "C", "D"}, Reachable({"A", "B", "C"})));
  // Only the tail reachable -> nothing older to land on.
  EXPECT_FALSE(CanBeginBackTraversal({"A", "B", "C", "D"}, Reachable({"D"})));
  EXPECT_FALSE(CanBeginBackTraversal({}, Reachable({})));
  EXPECT_FALSE(CanBeginBackTraversal({"A"}, Reachable({"A"})));
}

}  // namespace
}  // namespace roamux::tab_visit

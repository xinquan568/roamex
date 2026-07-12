// SPDX-License-Identifier: Apache-2.0
// roam-25 (I-4.5) mandatory matrix (§6.9): ONE journal append per traversal
// gesture (intermediate previews suppressed); the debounce fallback yields the
// same single append; command enablement tracks the traversal state; uid tail
// coalescing; Settle on a dead landed tab; CancelGesture clears suppression;
// cross-window landing keeps the session; flag-off inert. Drives the
// per-profile coordinator API directly so the core correctness is deterministic
// (no synthetic key events).

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/scoped_feature_list.h"
#include "base/time/time.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_user_gesture_details.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "roamux/browser/tab_visit/settled_visit_journal_factory.h"
#include "roamux/browser/tab_visit/settled_visit_journal_service.h"
#include "roamux/browser/tab_visit/tab_visit_traversal_coordinator.h"
#include "roamux/browser/tab_visit/tab_visit_traversal_coordinator_factory.h"
#include "roamux/browser/tab_visit/visits_store.h"
#include "roamux/browser/tabs/tab_uid_tab_helper.h"
#include "roamux/common/roamux_features.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace roamux {
namespace {

using tab_visit::SettledVisitJournalFactory;
using tab_visit::TabVisitTraversalCoordinator;
using tab_visit::TabVisitTraversalCoordinatorFactory;
using tab_visit::VisitRow;

std::vector<std::string> ReadVisitUrls(Profile* profile) {
  base::RunLoop run_loop;
  std::vector<std::string> urls;
  SettledVisitJournalFactory::GetForProfile(profile)->GetVisits(
      base::BindLambdaForTesting([&](std::vector<VisitRow> rows) {
        for (const VisitRow& row : rows) {
          urls.push_back(row.url);
        }
        run_loop.Quit();
      }));
  run_loop.Run();
  return urls;
}

class RoamuxTabVisitGestureTest : public InProcessBrowserTest {
 public:
  RoamuxTabVisitGestureTest() {
    features_.InitAndEnableFeature(features::kTabVisitNav);
  }

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(embedded_test_server()->Start());
  }

 protected:
  GURL UrlA() { return embedded_test_server()->GetURL("/title1.html"); }
  GURL UrlB() { return embedded_test_server()->GetURL("/title2.html"); }
  GURL UrlC() { return embedded_test_server()->GetURL("/title3.html"); }

  TabStripModel* tabs() { return browser()->tab_strip_model(); }
  TabVisitTraversalCoordinator* coordinator() {
    return TabVisitTraversalCoordinatorFactory::GetForProfile(
        browser()->profile());
  }

  // Switch to an existing tab (kSelectionOnly) so the bridge records a normal
  // settled visit (seeding both the url journal and the uid commit-log).
  void SettleOnTab(int index) {
    tabs()->ActivateTabAt(index,
                          TabStripUserGestureDetails(
                              TabStripUserGestureDetails::GestureType::kOther));
  }

  // Open three real-URL tabs and settle A -> B -> C so the log is [A,B,C] and C
  // is active. Returns the seeded journal length.
  size_t SeedThreeTabs() {
    EXPECT_TRUE(ui_test_utils::NavigateToURL(browser(), UrlA()));
    EXPECT_TRUE(AddTabAtIndex(1, UrlB(), ui::PAGE_TRANSITION_TYPED));
    EXPECT_TRUE(AddTabAtIndex(2, UrlC(), ui::PAGE_TRANSITION_TYPED));
    SettleOnTab(0);
    SettleOnTab(1);
    SettleOnTab(2);
    return ReadVisitUrls(browser()->profile()).size();
  }

  base::test::ScopedFeatureList features_;
};

// The core DoD: a multi-step gesture appends exactly ONE settled visit.
IN_PROC_BROWSER_TEST_F(RoamuxTabVisitGestureTest, OneAppendPerGesture) {
  const size_t seeded = SeedThreeTabs();  // journal [A,B,C]; active C.

  coordinator()->StepBack();     // preview -> B (suppressed)
  coordinator()->StepBack();     // preview -> A (suppressed)
  coordinator()->StepForward();  // preview -> B (suppressed)
  coordinator()->Settle();       // commit B exactly once

  const std::vector<std::string> urls = ReadVisitUrls(browser()->profile());
  EXPECT_EQ(urls.size(), seeded + 1);
  EXPECT_EQ(urls.back(), UrlB().spec());
  EXPECT_FALSE(coordinator()->IsTraversalActive());
}

// The debounce fallback (no explicit Settle) yields the same single append.
IN_PROC_BROWSER_TEST_F(RoamuxTabVisitGestureTest,
                       DebounceFallbackSameSingleAppend) {
  const size_t seeded = SeedThreeTabs();

  coordinator()->StepBack();  // -> B; arms the ~900ms debounce timer.
  coordinator()->StepBack();  // -> A; resets the timer.

  // Wait past the debounce so the coordinator's own timer settles the gesture.
  base::RunLoop loop;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, loop.QuitClosure(), base::Milliseconds(1500));
  loop.Run();

  EXPECT_FALSE(coordinator()->IsTraversalActive());
  const std::vector<std::string> urls = ReadVisitUrls(browser()->profile());
  EXPECT_EQ(urls.size(), seeded + 1);
  EXPECT_EQ(urls.back(), UrlA().spec());  // Settled on the last previewed tab.
}

// Command enablement reflects the traversal state.
IN_PROC_BROWSER_TEST_F(RoamuxTabVisitGestureTest, CommandEnablement) {
  SeedThreeTabs();  // active C (tail); older reachable tabs exist.
  EXPECT_TRUE(chrome::IsCommandEnabled(browser(), IDC_TAB_VISIT_BACK));
  EXPECT_FALSE(chrome::IsCommandEnabled(browser(), IDC_TAB_VISIT_FORWARD));

  coordinator()->StepBack();  // now mid-gesture, one step back.
  EXPECT_TRUE(coordinator()->CanGoBack());
  EXPECT_TRUE(coordinator()->CanGoForward());
  coordinator()->Settle();
}

// uid tail coalescing: settling the already-current tail adds no entry.
IN_PROC_BROWSER_TEST_F(RoamuxTabVisitGestureTest, TailCoalescing) {
  const size_t seeded = SeedThreeTabs();  // tail C active.
  // Re-settle on C (the tail) via a normal activation -> coalesced, no append.
  SettleOnTab(2);
  EXPECT_EQ(ReadVisitUrls(browser()->profile()).size(), seeded);
}

// CancelGesture clears suppression so the next normal activation appends again.
IN_PROC_BROWSER_TEST_F(RoamuxTabVisitGestureTest,
                       CancelGestureClearsSuppression) {
  const size_t seeded = SeedThreeTabs();
  coordinator()->StepBack();  // enter a gesture (bridge now suppressed).
  ASSERT_TRUE(coordinator()->IsTraversalActive());
  coordinator()->CancelGesture();
  EXPECT_FALSE(coordinator()->IsTraversalActive());

  // A normal settle now appends (suppression is not stuck).
  SettleOnTab(0);
  const std::vector<std::string> urls = ReadVisitUrls(browser()->profile());
  EXPECT_EQ(urls.size(), seeded + 1);
  EXPECT_EQ(urls.back(), UrlA().spec());
}

// A stale-enabled Forward after settling must NOT re-append (F1): pre-gesture
// Forward is disabled, so the coordinator no-ops it instead of committing the
// current tail again.
IN_PROC_BROWSER_TEST_F(RoamuxTabVisitGestureTest,
                       StaleForwardAfterSettleIsNoOp) {
  SeedThreeTabs();
  coordinator()->StepBack();  // -> B
  coordinator()->Settle();    // commit B
  const size_t after_settle = ReadVisitUrls(browser()->profile()).size();

  // Simulate a stale-enabled Forward press after the gesture ended.
  coordinator()->StepForward();
  EXPECT_FALSE(coordinator()->IsTraversalActive());
  EXPECT_EQ(ReadVisitUrls(browser()->profile()).size(), after_settle);
}

// If the landed tab is closed before settle, nothing is committed (F2).
IN_PROC_BROWSER_TEST_F(RoamuxTabVisitGestureTest, SettleOnDeadLandedTab) {
  SeedThreeTabs();            // journal [A,B,C]; tabs A,B,C; active C.
  coordinator()->StepBack();  // -> B (index 1) becomes active.
  ASSERT_EQ(1, tabs()->active_index());

  // Close the landed tab (still mid-gesture, so the bridge is suppressed).
  tabs()->CloseWebContentsAt(1, TabCloseTypes::CLOSE_USER_GESTURE);
  const size_t before = ReadVisitUrls(browser()->profile()).size();

  coordinator()->Settle();  // landed uid no longer resolvable -> no commit.
  EXPECT_FALSE(coordinator()->IsTraversalActive());
  EXPECT_EQ(ReadVisitUrls(browser()->profile()).size(), before);
}

// Closing a window mid-gesture must not strand suppression: the per-profile
// debounce timer still settles and clears the flag (F2).
IN_PROC_BROWSER_TEST_F(RoamuxTabVisitGestureTest,
                       WindowCloseDuringGestureDoesNotStrandSuppression) {
  SeedThreeTabs();
  Browser* second = CreateBrowser(browser()->profile());
  coordinator()->StepBack();  // gesture active (bridge now suppressed).
  ASSERT_TRUE(coordinator()->IsTraversalActive());

  CloseBrowserSynchronously(second);  // close a window mid-gesture.

  // The coordinator's own debounce timer settles the gesture regardless.
  base::RunLoop loop;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, loop.QuitClosure(), base::Milliseconds(1500));
  loop.Run();
  EXPECT_FALSE(coordinator()->IsTraversalActive());

  // Suppression is not stuck: a normal activation appends again.
  const size_t before = ReadVisitUrls(browser()->profile()).size();
  SettleOnTab(0);
  EXPECT_EQ(ReadVisitUrls(browser()->profile()).size(), before + 1);
}

// Flag-off: the commands are disabled and traversal is inert.
class RoamuxTabVisitGestureFlagOffTest : public InProcessBrowserTest {
 public:
  RoamuxTabVisitGestureFlagOffTest() {
    features_.InitAndDisableFeature(features::kTabVisitNav);
  }

 protected:
  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(RoamuxTabVisitGestureFlagOffTest, CommandsDisabled) {
  EXPECT_FALSE(chrome::IsCommandEnabled(browser(), IDC_TAB_VISIT_BACK));
  EXPECT_FALSE(chrome::IsCommandEnabled(browser(), IDC_TAB_VISIT_FORWARD));
}

}  // namespace
}  // namespace roamux

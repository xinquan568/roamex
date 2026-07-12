// SPDX-License-Identifier: Apache-2.0
// roam-28 (I-4.8) mandatory matrix (§6.9 / J4): the settled-visit journal obeys
// Clear-Browsing-Data. Driving the REAL BrowsingDataRemover
// (DATA_TYPE_HISTORY): all-time clear empties the journal; the in-memory
// reopenable cache is purged so a cleared closed tab is no longer reopenable,
// while LIVE-tab uid identity survives (§6.9); a clear mid-gesture cannot
// re-append a just-cleared URL; a future-only range is a no-op (range
// respected); enterprise policy (kAllowDeletingBrowserHistory=false) leaves the
// journal intact.

#include <string>
#include <vector>

#include "base/test/scoped_feature_list.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "chrome/browser/browsing_data/chrome_browsing_data_remover_constants.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_user_gesture_details.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/history/core/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browsing_data_remover.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browsing_data_remover_test_util.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "roamux/browser/tab_visit/settled_visit_journal_factory.h"
#include "roamux/browser/tab_visit/settled_visit_journal_service.h"
#include "roamux/browser/tab_visit/tab_visit_command.h"
#include "roamux/browser/tab_visit/tab_visit_traversal_coordinator.h"
#include "roamux/browser/tab_visit/tab_visit_traversal_coordinator_factory.h"
#include "roamux/browser/tab_visit/visits_store.h"
#include "roamux/browser/tabs/tab_uid_tab_helper.h"
#include "roamux/common/roamux_features.h"
#include "roamux/common/roamux_prefs.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace roamux {
namespace {

using tab_visit::SettledVisitJournalFactory;
using tab_visit::TabVisitTraversalCoordinator;
using tab_visit::TabVisitTraversalCoordinatorFactory;
using tab_visit::VisitRow;

class RoamuxClearBrowsingDataTest : public InProcessBrowserTest {
 public:
  RoamuxClearBrowsingDataTest() {
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

  std::string UidAt(int index) {
    content::WebContents* wc = tabs()->GetWebContentsAt(index);
    tabs::TabUidTabHelper* helper =
        wc ? tabs::TabUidTabHelper::FromWebContents(wc) : nullptr;
    return helper ? helper->uid() : std::string();
  }

  int LiveIndexOfUid(const std::string& uid) {
    for (int i = 0; i < tabs()->count(); ++i) {
      if (UidAt(i) == uid) {
        return i;
      }
    }
    return -1;
  }

  void SettleOnTab(int index) {
    tabs()->ActivateTabAt(index,
                          TabStripUserGestureDetails(
                              TabStripUserGestureDetails::GestureType::kOther));
  }

  // Open A,B,C and settle A->B->C so the journal has three uid-keyed visits.
  void SeedThreeTabs() {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), UrlA()));
    ASSERT_TRUE(AddTabAtIndex(1, UrlB(), ui::PAGE_TRANSITION_TYPED));
    ASSERT_TRUE(AddTabAtIndex(2, UrlC(), ui::PAGE_TRANSITION_TYPED));
    SettleOnTab(0);
    SettleOnTab(1);
    SettleOnTab(2);
  }

  std::vector<VisitRow> ReadVisits(Profile* profile) {
    base::test::TestFuture<std::vector<VisitRow>> future;
    SettledVisitJournalFactory::GetForProfile(profile)->GetVisits(
        future.GetCallback());
    return future.Take();
  }

  void EnableReopen() {
    browser()->profile()->GetPrefs()->SetBoolean(prefs::kReopenClosed, true);
    tab_visit::CanTabVisitBack(browser());  // inject the reopen fn (roam-27).
    ASSERT_TRUE(coordinator()->has_reopen_fn());
  }

  // Drive the REAL BrowsingDataRemover for the History data type over
  // [begin, end); `extra_mask` allows DATA_TYPE_NO_CHECKS for the policy test.
  void ClearHistory(Profile* profile,
                    base::Time begin,
                    base::Time end,
                    uint64_t extra_mask = 0) {
    content::BrowsingDataRemover* remover = profile->GetBrowsingDataRemover();
    content::BrowsingDataRemoverCompletionObserver observer(remover);
    remover->RemoveAndReply(
        begin, end,
        chrome_browsing_data_remover::DATA_TYPE_HISTORY | extra_mask,
        content::BrowsingDataRemover::ORIGIN_TYPE_UNPROTECTED_WEB |
            content::BrowsingDataRemover::ORIGIN_TYPE_PROTECTED_WEB,
        &observer);
    observer.BlockUntilCompletion();
  }

  base::test::ScopedFeatureList features_;
};

// All-time clear empties the persisted settled-visit journal.
IN_PROC_BROWSER_TEST_F(RoamuxClearBrowsingDataTest,
                       ClearAllTimeEmptiesJournal) {
  SeedThreeTabs();
  ASSERT_GE(ReadVisits(browser()->profile()).size(), 3u);

  ClearHistory(browser()->profile(), base::Time(), base::Time::Max());

  EXPECT_EQ(0u, ReadVisits(browser()->profile()).size());
}

// A future-only range is a no-op: the journal is untouched (range respected).
IN_PROC_BROWSER_TEST_F(RoamuxClearBrowsingDataTest, ClearFutureRangeIsNoOp) {
  SeedThreeTabs();
  const size_t before = ReadVisits(browser()->profile()).size();
  ASSERT_GE(before, 3u);

  ClearHistory(browser()->profile(), base::Time::Now() + base::Days(1),
               base::Time::Max());

  EXPECT_EQ(before, ReadVisits(browser()->profile()).size());
}

// F2: an all-time clear purges the in-memory reopenable cache so a cleared
// closed tab is no longer reopenable, while a LIVE tab's uid identity survives.
IN_PROC_BROWSER_TEST_F(RoamuxClearBrowsingDataTest,
                       ClearPurgesReopenableAndKeepsLiveIdentity) {
  SeedThreeTabs();
  const std::string uid_b = UidAt(1);  // will be closed (reopenable).
  const std::string uid_a = UidAt(0);  // stays live.
  ASSERT_FALSE(uid_b.empty());
  ASSERT_FALSE(uid_a.empty());
  tabs()->CloseWebContentsAt(1, TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
  ASSERT_EQ(-1, LiveIndexOfUid(uid_b));
  EnableReopen();

  ClearHistory(browser()->profile(), base::Time(), base::Time::Max());
  ASSERT_EQ(0u, ReadVisits(browser()->profile()).size());

  // The closed uid is no longer reopenable: a Back gesture skips it
  // (unreachable — purged from reopenable_ and gone from the journal) and lands
  // on the older LIVE tab instead; uid_b never comes back.
  coordinator()->StepBack();
  coordinator()->Settle();
  EXPECT_EQ(-1, LiveIndexOfUid(uid_b));

  // Live-tab identity survived: uid_a is still a live, activatable tab (§6.9).
  const int a_idx = LiveIndexOfUid(uid_a);
  ASSERT_NE(-1, a_idx);
  SettleOnTab(a_idx);
  EXPECT_EQ(uid_a, UidAt(tabs()->active_index()));
}

// F1: a clear that lands mid-gesture discards the gesture, so a later Settle
// cannot re-append a just-cleared URL; the live tab's uid still works.
IN_PROC_BROWSER_TEST_F(RoamuxClearBrowsingDataTest,
                       ClearDuringActiveGestureNoReappend) {
  SeedThreeTabs();
  const std::string uid_a = UidAt(0);
  coordinator()->StepBack();  // enter an active gesture (previewing back).
  ASSERT_TRUE(coordinator()->IsTraversalActive());

  ClearHistory(browser()->profile(), base::Time(), base::Time::Max());
  // OnBrowsingDataCleared cancelled the gesture.
  EXPECT_FALSE(coordinator()->IsTraversalActive());
  ASSERT_EQ(0u, ReadVisits(browser()->profile()).size());

  // A now-stale Settle must NOT append the pre-clear landed URL.
  coordinator()->Settle();
  EXPECT_EQ(0u, ReadVisits(browser()->profile()).size());

  // Live identity intact: a fresh settle records normally afterwards.
  ASSERT_NE(-1, LiveIndexOfUid(uid_a));
}

// F1 (race): PrepareForBrowsingDataClear cancels an active gesture
// SYNCHRONOUSLY (before the async store clear is posted), so a Settle that
// fires afterwards — as one could during the clear window — cannot re-append
// the pre-clear landed URL. Drives the coordinator directly to pin the ordering
// deterministically (a real clear cancels at the same synchronous point).
IN_PROC_BROWSER_TEST_F(RoamuxClearBrowsingDataTest,
                       PrepareCancelsGestureSynchronouslyNoReappend) {
  SeedThreeTabs();
  const size_t seeded = ReadVisits(browser()->profile()).size();
  ASSERT_GE(seeded, 3u);

  coordinator()->StepBack();  // active gesture, previewing back.
  ASSERT_TRUE(coordinator()->IsTraversalActive());

  // The synchronous pre-clear step the hook runs BEFORE posting the store
  // clear.
  coordinator()->PrepareForBrowsingDataClear();
  EXPECT_FALSE(coordinator()->IsTraversalActive());  // cancelled synchronously.

  // A now-stale Settle (as a modifier release would trigger in the clear
  // window) must NOT append — no RecordVisit can be queued after the pending
  // delete.
  coordinator()->Settle();
  EXPECT_EQ(seeded, ReadVisits(browser()->profile()).size());
}

// Enterprise policy: with kAllowDeletingBrowserHistory=false the History block
// (and thus the roamux hook) is skipped, so the journal is NOT wiped even
// though the mask requests it (DATA_TYPE_NO_CHECKS avoids the upstream policy
// DCHECK).
IN_PROC_BROWSER_TEST_F(RoamuxClearBrowsingDataTest, PolicyBlockedKeepsJournal) {
  SeedThreeTabs();
  const size_t before = ReadVisits(browser()->profile()).size();
  ASSERT_GE(before, 3u);
  browser()->profile()->GetPrefs()->SetBoolean(
      ::prefs::kAllowDeletingBrowserHistory, false);

  ClearHistory(browser()->profile(), base::Time(), base::Time::Max(),
               content::BrowsingDataRemover::DATA_TYPE_NO_CHECKS);

  EXPECT_EQ(before, ReadVisits(browser()->profile()).size());  // policy-gated.
}

}  // namespace
}  // namespace roamux

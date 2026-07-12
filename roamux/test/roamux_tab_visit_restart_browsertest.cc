// SPDX-License-Identifier: Apache-2.0
// roam-26 (I-4.6): the settled-visit journal + its uid-keyed MRU survive a full
// restart. SessionRestore rebinds each tab's durable uid (roam-10 extra_data),
// and the coordinator reloads the persisted journal into the MRU commit-log on
// startup — so Back/Forward step in the same visit order after a relaunch and
// the Back command becomes enabled once the async load completes (no extra
// activation needed). PRE_ seeds a persistent profile; the twin reads it.

#include <set>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/run_loop.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/test_future.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_user_gesture_details.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "roamux/browser/tab_visit/settled_visit_journal_factory.h"
#include "roamux/browser/tab_visit/settled_visit_journal_service.h"
#include "roamux/browser/tab_visit/tab_visit_traversal_coordinator.h"
#include "roamux/browser/tab_visit/tab_visit_traversal_coordinator_factory.h"
#include "roamux/browser/tab_visit/visits_store.h"
#include "roamux/common/roamux_features.h"

namespace roamux {
namespace {

using tab_visit::TabVisitTraversalCoordinator;
using tab_visit::TabVisitTraversalCoordinatorFactory;

size_t DistinctNonEmptyUids(const std::vector<tab_visit::VisitRow>& visits) {
  std::set<std::string> uids;
  for (const tab_visit::VisitRow& v : visits) {
    if (!v.tab_uid.empty()) {
      uids.insert(v.tab_uid);
    }
  }
  return uids.size();
}

class RoamuxTabVisitRestartTest : public InProcessBrowserTest {
 public:
  RoamuxTabVisitRestartTest() {
    features_.InitAndEnableFeature(features::kTabVisitNav);
  }

  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
    SessionStartupPref::SetStartupPref(
        browser()->profile(), SessionStartupPref(SessionStartupPref::LAST));
    InProcessBrowserTest::SetUpOnMainThread();
  }

 protected:
  TabVisitTraversalCoordinator* coordinator() {
    return TabVisitTraversalCoordinatorFactory::GetForProfile(
        browser()->profile());
  }

  // Block until the coordinator has reloaded the persisted journal at startup.
  void WaitForJournalLoaded() {
    if (coordinator()->IsJournalLoaded()) {
      return;
    }
    base::RunLoop run_loop;
    auto sub = coordinator()->AddJournalLoadedCallback(run_loop.QuitClosure());
    run_loop.Run();
  }

  void SettleOnTab(int index) {
    browser()->tab_strip_model()->ActivateTabAt(
        index, TabStripUserGestureDetails(
                   TabStripUserGestureDetails::GestureType::kOther));
  }

  // Reads the persisted journal synchronously (draining the async store — a
  // read is sequenced after all prior RecordVisit writes, so this also flushes
  // pending writes to disk). Returns the persisted visits.
  std::vector<tab_visit::VisitRow> ReadJournal() {
    base::test::TestFuture<std::vector<tab_visit::VisitRow>> future;
    tab_visit::SettledVisitJournalFactory::GetForProfile(browser()->profile())
        ->GetVisits(future.GetCallback());
    return future.Take();
  }

  // Reads the persisted tab-state sidecar synchronously (same drain semantics).
  std::vector<tab_visit::TabStateRow> ReadTabStates() {
    base::test::TestFuture<std::vector<tab_visit::TabStateRow>> future;
    tab_visit::SettledVisitJournalFactory::GetForProfile(browser()->profile())
        ->GetAllTabStates(future.GetCallback());
    return future.Take();
  }

  base::test::ScopedFeatureList features_;
};

// PRE_: seed a persistent profile — three real-URL tabs settled A->B->C (so the
// uid-keyed journal has three entries) and one tab closed (so a sidecar closed
// row is persisted).
IN_PROC_BROWSER_TEST_F(RoamuxTabVisitRestartTest,
                       PRE_RestartReloadsUidJournal) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  ASSERT_TRUE(AddTabAtIndex(1, embedded_test_server()->GetURL("/title2.html"),
                            ui::PAGE_TRANSITION_TYPED));
  ASSERT_TRUE(AddTabAtIndex(2, embedded_test_server()->GetURL("/title3.html"),
                            ui::PAGE_TRANSITION_TYPED));
  SettleOnTab(0);
  SettleOnTab(1);
  SettleOnTab(2);
  // Close a tab so the structural sidecar writer persists a closed row.
  browser()->tab_strip_model()->CloseWebContentsAt(
      2, TabCloseTypes::CLOSE_USER_GESTURE);

  // Drain both async stores so the uid-keyed visits AND the closed sidecar row
  // are committed to disk before this PRE_ browser tears down (else the twin
  // reloads an empty journal).
  ASSERT_GE(ReadJournal().size(), 3u);
  ASSERT_GE(ReadTabStates().size(), 1u);
}

// After the restart: the coordinator reloads BOTH persisted tables — the
// uid-keyed journal (so the derived-MRU that traversal walks survives) and the
// tab-state sidecar (the closed row) — deterministically, read straight from
// the reloaded on-disk store. (The live end-to-end Back/Forward rebind to the
// restored tabs additionally relies on roam-10's durable-uid session-restore
// adopt re-binding each restored tab to its persisted uid; the reachable-uid
// filter + traversal mechanics themselves are proven by roam-24/25. This test
// pins roam-26's contribution: the uid-keyed journal + sidecar persist and
// reload. The NoRestore twin below pins the "no live match -> Back disabled"
// failure mode.)
IN_PROC_BROWSER_TEST_F(RoamuxTabVisitRestartTest, RestartReloadsUidJournal) {
  TabStripModel* tabs = browser()->tab_strip_model();
  for (int i = 0; i < tabs->count(); ++i) {
    ASSERT_TRUE(content::WaitForLoadStop(tabs->GetWebContentsAt(i)));
  }

  WaitForJournalLoaded();

  // The uid-keyed journal reloaded with its persisted visits, each carrying the
  // v3 durable-uid column.
  const std::vector<tab_visit::VisitRow> visits = ReadJournal();
  EXPECT_GE(visits.size(), 3u);
  EXPECT_FALSE(visits.back().tab_uid.empty());

  // The tab-state sidecar reloaded with the persisted closed row (keyed by the
  // durable uid).
  const std::vector<tab_visit::TabStateRow> states = ReadTabStates();
  ASSERT_GE(states.size(), 1u);
  EXPECT_TRUE(states.front().closed);
  EXPECT_FALSE(states.front().restore_key.empty());  // uid-keyed.
}

// Failure mode (no matching live tabs): a persisted journal whose uids are not
// live after a fresh (non-restoring) launch yields a disabled Back — the
// reachable-live-uid filter skips every non-restored uid, no special-casing.
class RoamuxTabVisitNoRestoreTest : public InProcessBrowserTest {
 public:
  RoamuxTabVisitNoRestoreTest() {
    features_.InitAndEnableFeature(features::kTabVisitNav);
  }
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
    InProcessBrowserTest::SetUpOnMainThread();
  }

 protected:
  TabVisitTraversalCoordinator* coordinator() {
    return TabVisitTraversalCoordinatorFactory::GetForProfile(
        browser()->profile());
  }
  std::vector<tab_visit::VisitRow> ReadJournal() {
    base::test::TestFuture<std::vector<tab_visit::VisitRow>> future;
    tab_visit::SettledVisitJournalFactory::GetForProfile(browser()->profile())
        ->GetVisits(future.GetCallback());
    return future.Take();
  }
  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(RoamuxTabVisitNoRestoreTest,
                       PRE_NoRestoreLeavesBackDisabled) {
  // Seed a journal, but do NOT arm session restore (default startup pref), so
  // next launch is a fresh window whose tabs do not carry these uids.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  ASSERT_TRUE(AddTabAtIndex(1, embedded_test_server()->GetURL("/title2.html"),
                            ui::PAGE_TRANSITION_TYPED));
  // Settle on TWO different tabs so the journal holds two distinct uids — the
  // reloaded MRU then has an older-than-tail entry, so the twin's disabled-Back
  // check exercises the reachable-live filter (not an anchor-only MRU that is
  // trivially non-traversable).
  browser()->tab_strip_model()->ActivateTabAt(
      0, TabStripUserGestureDetails(
             TabStripUserGestureDetails::GestureType::kOther));
  browser()->tab_strip_model()->ActivateTabAt(
      1, TabStripUserGestureDetails(
             TabStripUserGestureDetails::GestureType::kOther));
  // Drain so the seeded visits commit before teardown, and require >=2 distinct
  // non-empty uids so OnJournalLoaded can't collapse the MRU to a single
  // anchor.
  ASSERT_GE(DistinctNonEmptyUids(ReadJournal()), 2u);
}

IN_PROC_BROWSER_TEST_F(RoamuxTabVisitNoRestoreTest,
                       NoRestoreLeavesBackDisabled) {
  if (!coordinator()->IsJournalLoaded()) {
    base::RunLoop run_loop;
    auto sub = coordinator()->AddJournalLoadedCallback(run_loop.QuitClosure());
    run_loop.Run();
  }
  // The journal reloaded with two distinct uid-keyed visits (a non-empty MRU
  // WITH an older-than-tail entry), so Back being disabled proves the
  // reachable-live-uid filter excludes the non-restored persisted uids — not
  // that the MRU was empty or anchor-only.
  ASSERT_GE(DistinctNonEmptyUids(ReadJournal()), 2u);
  EXPECT_FALSE(coordinator()->CanGoBack());
}

}  // namespace
}  // namespace roamux

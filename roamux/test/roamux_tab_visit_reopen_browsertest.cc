// SPDX-License-Identifier: Apache-2.0
// roam-27 (I-4.7) mandatory matrix (§6.6): with `reopen_closed` ON, traversal
// onto a CLOSED tab's durable uid REOPENS the exact tab (uid live again + its
// sidecar row flips closed=false), disambiguating same-URL siblings by uid;
// an eviction fallback re-creates the tab at its last_known_url re-stamped with
// the old uid; a closed WINDOW entry's nested tab is reopenable by uid; and
// with reopen OFF (default) a closed uid stays unreachable (today's behavior).
// Drives the per-profile coordinator API directly so the core correctness is
// deterministic (mirrors roam-25/26).

#include <string>
#include <vector>

#include "base/test/scoped_feature_list.h"
#include "base/test/test_future.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/tab_restore_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_user_gesture_details.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "components/sessions/core/tab_restore_service.h"
#include "components/sessions/core/tab_restore_types.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
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
using tab_visit::TabStateRow;
using tab_visit::TabVisitTraversalCoordinator;
using tab_visit::TabVisitTraversalCoordinatorFactory;
using tab_visit::VisitRow;

class RoamuxTabVisitReopenTest : public InProcessBrowserTest {
 public:
  RoamuxTabVisitReopenTest() {
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

  static std::string UidOf(content::WebContents* web_contents) {
    tabs::TabUidTabHelper* helper =
        web_contents ? tabs::TabUidTabHelper::FromWebContents(web_contents)
                     : nullptr;
    return helper ? helper->uid() : std::string();
  }

  std::string UidAt(int index) {
    return UidOf(tabs()->GetWebContentsAt(index));
  }

  // Index of the live tab in browser()'s strip with `uid`, or -1.
  int LiveIndexOfUid(const std::string& uid) {
    for (int i = 0; i < tabs()->count(); ++i) {
      if (UidAt(i) == uid) {
        return i;
      }
    }
    return -1;
  }

  // True if any live tab in ANY window of the profile has `uid`.
  bool IsUidLiveInProfile(const std::string& uid) {
    for (BrowserWindowInterface* b : GetAllBrowserWindowInterfaces()) {
      if (b->GetProfile() != browser()->profile()) {
        continue;
      }
      TabStripModel* model = b->GetTabStripModel();
      for (int i = 0; i < model->count(); ++i) {
        if (UidOf(model->GetWebContentsAt(i)) == uid) {
          return true;
        }
      }
    }
    return false;
  }

  void SettleOnTab(int index) {
    tabs()->ActivateTabAt(index,
                          TabStripUserGestureDetails(
                              TabStripUserGestureDetails::GestureType::kOther));
  }

  // Turn the pref ON and force the ui layer to inject the reopen action into
  // the coordinator (the coordinator cannot reach TabRestoreService itself —
  // the command shim injects it lazily on first reach).
  void EnableReopen() {
    browser()->profile()->GetPrefs()->SetBoolean(prefs::kReopenClosed, true);
    tab_visit::CanTabVisitBack(browser());  // triggers the lazy injection.
    ASSERT_TRUE(coordinator()->has_reopen_fn());
  }

  std::vector<TabStateRow> ReadTabStates() {
    base::test::TestFuture<std::vector<TabStateRow>> future;
    SettledVisitJournalFactory::GetForProfile(browser()->profile())
        ->GetAllTabStates(future.GetCallback());
    return future.Take();
  }

  // Drains the persisted uid-keyed visit journal (a read is sequenced after all
  // prior RecordVisit writes, so this flushes them too).
  std::vector<VisitRow> ReadVisits() {
    base::test::TestFuture<std::vector<VisitRow>> future;
    SettledVisitJournalFactory::GetForProfile(browser()->profile())
        ->GetVisits(future.GetCallback());
    return future.Take();
  }

  static const TabStateRow* FindState(const std::vector<TabStateRow>& rows,
                                      const std::string& uid) {
    for (const TabStateRow& row : rows) {
      if (row.restore_key == uid) {
        return &row;
      }
    }
    return nullptr;
  }

  // True iff a TabRestoreService entry (a TAB, or a nested tab of a WINDOW
  // entry) still carries the uid extra_data for `uid`. An EXACT reopen CONSUMES
  // the entry (RestoreEntryById removes it); the eviction fallback never
  // touches the restore list — so this is the exact-vs-fallback discriminator.
  bool RestoreEntryExistsForUid(const std::string& uid) {
    sessions::TabRestoreService* svc =
        TabRestoreServiceFactory::GetForProfile(browser()->profile());
    if (!svc) {
      return false;
    }
    const char* key = tabs::TabUidTabHelper::kExtraDataKey;
    for (const auto& entry : svc->entries()) {
      if (entry->type == sessions::tab_restore::TAB) {
        auto it = entry->extra_data.find(key);
        if (it != entry->extra_data.end() && it->second == uid) {
          return true;
        }
      } else if (entry->type == sessions::tab_restore::WINDOW) {
        const auto* window =
            static_cast<const sessions::tab_restore::Window*>(entry.get());
        for (const auto& tab : window->tabs) {
          auto it = tab->extra_data.find(key);
          if (it != tab->extra_data.end() && it->second == uid) {
            return true;
          }
        }
      }
    }
    return false;
  }

  // Open A,B,C and settle A->B->C so the commit-log is [A,B,C] and C is active.
  void SeedThreeTabs() {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), UrlA()));
    ASSERT_TRUE(AddTabAtIndex(1, UrlB(), ui::PAGE_TRANSITION_TYPED));
    ASSERT_TRUE(AddTabAtIndex(2, UrlC(), ui::PAGE_TRANSITION_TYPED));
    SettleOnTab(0);
    SettleOnTab(1);
    SettleOnTab(2);
  }

  base::test::ScopedFeatureList features_;
};

// reopen ON: Back onto a closed tab's uid reopens the EXACT tab (original uid
// live again) and flips its sidecar row to closed=false.
IN_PROC_BROWSER_TEST_F(RoamuxTabVisitReopenTest, ReopenExactTabByUid) {
  SeedThreeTabs();
  const std::string uid_b = UidAt(1);
  ASSERT_FALSE(uid_b.empty());

  // Close B (historical, so the restore entry carries the uid extra_data).
  tabs()->CloseWebContentsAt(1, TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
  ASSERT_EQ(-1, LiveIndexOfUid(uid_b));
  ASSERT_TRUE(
      RestoreEntryExistsForUid(uid_b));  // an EXACT restore entry exists.

  EnableReopen();
  coordinator()->StepBack();  // C -> B (closed) -> reopen the exact tab.
  coordinator()->Settle();

  const int reopened = LiveIndexOfUid(uid_b);
  ASSERT_NE(-1, reopened);  // B is live again with its ORIGINAL uid.
  EXPECT_EQ(UrlB(), tabs()->GetWebContentsAt(reopened)->GetLastCommittedURL());
  // Prove the EXACT path ran (not the fallback): RestoreEntryById CONSUMED the
  // restore entry. Were extra_data matching broken, the fallback would have
  // produced the same live uid/URL while leaving the entry in place.
  EXPECT_FALSE(RestoreEntryExistsForUid(uid_b));

  // The single sidecar row for B flipped closed=false (the rebind).
  const std::vector<TabStateRow> states = ReadTabStates();
  const TabStateRow* row = FindState(states, uid_b);
  ASSERT_TRUE(row);
  EXPECT_FALSE(row->closed);
}

// Same-URL disambiguation: two same-URL tabs (distinct uids); closing one and
// traversing to it reopens THAT uid (a new tab), NOT a re-activation of the
// still-open same-URL sibling — because the controller resolves by uid.
IN_PROC_BROWSER_TEST_F(RoamuxTabVisitReopenTest, SameUrlDisambiguation) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), UrlA()));  // tab0: uid P
  ASSERT_TRUE(AddTabAtIndex(1, UrlA(), ui::PAGE_TRANSITION_TYPED));  // uid Q
  ASSERT_TRUE(AddTabAtIndex(2, UrlC(), ui::PAGE_TRANSITION_TYPED));  // anchor
  const std::string uid_p = UidAt(0);
  const std::string uid_q = UidAt(1);
  ASSERT_FALSE(uid_q.empty());
  ASSERT_NE(uid_p, uid_q);
  SettleOnTab(0);
  SettleOnTab(1);
  SettleOnTab(2);  // log [P,Q,R]; active C.

  tabs()->CloseWebContentsAt(1, TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
  ASSERT_NE(-1, LiveIndexOfUid(uid_p));  // the same-URL sibling stays open.
  ASSERT_EQ(-1, LiveIndexOfUid(uid_q));
  ASSERT_TRUE(RestoreEntryExistsForUid(uid_q));

  EnableReopen();
  coordinator()->StepBack();  // C -> Q (closed) -> reopen Q, not P.
  coordinator()->Settle();

  const int q_idx = LiveIndexOfUid(uid_q);
  const int p_idx = LiveIndexOfUid(uid_p);
  ASSERT_NE(-1, q_idx);  // a NEW live tab with uid Q.
  ASSERT_NE(-1, p_idx);  // the sibling P is still live and distinct.
  EXPECT_NE(p_idx, q_idx);
  EXPECT_EQ(UrlA(), tabs()->GetWebContentsAt(q_idx)->GetLastCommittedURL());
  // uid Q's own entry was consumed by the EXACT restore — the reopen resolved
  // by uid, not by matching P's identical URL.
  EXPECT_FALSE(RestoreEntryExistsForUid(uid_q));
}

// Eviction fallback: with the exact restore entry gone, traversal opens a fresh
// tab at last_known_url re-stamped with the old uid (journal rebinds).
IN_PROC_BROWSER_TEST_F(RoamuxTabVisitReopenTest, EvictionFallbackReStampsUid) {
  SeedThreeTabs();
  const std::string uid_b = UidAt(1);
  ASSERT_FALSE(uid_b.empty());

  tabs()->CloseWebContentsAt(1, TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
  ASSERT_EQ(-1, LiveIndexOfUid(uid_b));

  // Evict the restore entry so the EXACT path CANNOT match and the fallback
  // must run. (Empty restore list ⇒ the live uid below can only come from
  // fallback.)
  sessions::TabRestoreService* svc =
      TabRestoreServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(svc);
  svc->ClearEntries();
  ASSERT_FALSE(RestoreEntryExistsForUid(uid_b));

  // Baseline journal length so we can assert EXACTLY ONE new visit is appended
  // for the fallback landing (its navigation is async — regression guard for
  // the "fallback settles without persisting a visit" bug).
  const size_t visits_before = ReadVisits().size();

  EnableReopen();
  coordinator()->StepBack();  // C -> B (closed, evicted) -> fallback re-create.
  coordinator()->Settle();

  const int reopened = LiveIndexOfUid(uid_b);
  ASSERT_NE(-1, reopened);  // uid re-stamped onto a fresh tab (adopted).
  content::WebContents* wc = tabs()->GetWebContentsAt(reopened);
  EXPECT_TRUE(content::WaitForLoadStop(wc));  // fallback navigation is async.
  EXPECT_EQ(UrlB(), wc->GetLastCommittedURL());

  // The settled visit was persisted from last_known_url even though the
  // fallback navigation had not committed at Settle time.
  const std::vector<VisitRow> visits = ReadVisits();
  ASSERT_EQ(visits_before + 1u, visits.size());
  EXPECT_EQ(uid_b, visits.back().tab_uid);
  EXPECT_EQ(UrlB().spec(), visits.back().url);
}

// A closed WINDOW entry's nested tab is reopenable by its uid THROUGH the
// traversal coordinator: exactly the matched nested tab restores (its sibling
// does NOT), and its sidecar row rebinds to closed=false.
IN_PROC_BROWSER_TEST_F(RoamuxTabVisitReopenTest,
                       ReopenTabFromClosedWindowByUid) {
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), UrlA()));  // window1 anchor
  const std::string uid_a = UidAt(0);
  ASSERT_FALSE(uid_a.empty());

  // A second window with TWO real-URL tabs; capture both uids so we can prove
  // only the matched one comes back.
  Browser* w2 = CreateBrowser(browser()->profile());
  ASSERT_TRUE(AddTabAtIndexToBrowser(w2, 1, UrlB(), ui::PAGE_TRANSITION_TYPED,
                                     /*check_navigation_success=*/true));
  ASSERT_TRUE(AddTabAtIndexToBrowser(w2, 2, UrlC(), ui::PAGE_TRANSITION_TYPED,
                                     /*check_navigation_success=*/true));
  const std::string uid_w = UidOf(w2->tab_strip_model()->GetWebContentsAt(1));
  const std::string uid_other =
      UidOf(w2->tab_strip_model()->GetWebContentsAt(2));
  ASSERT_FALSE(uid_w.empty());
  ASSERT_FALSE(uid_other.empty());
  ASSERT_NE(uid_w, uid_other);

  // Close the whole window -> a WINDOW restore entry with nested tabs, each
  // carrying its uid extra_data (roam-10 patch 0009 PopulateExtraData); the
  // bridge registers each as reopenable + persists a closed sidecar row.
  CloseBrowserSynchronously(w2);
  ASSERT_FALSE(IsUidLiveInProfile(uid_w));
  ASSERT_FALSE(IsUidLiveInProfile(uid_other));
  // Both tabs live nested inside the closed WINDOW restore entry.
  ASSERT_TRUE(RestoreEntryExistsForUid(uid_w));
  ASSERT_TRUE(RestoreEntryExistsForUid(uid_other));

  EnableReopen();
  // Seed the commit-log [uid_w, uid_a] so a Back from the live anchor uid_a
  // lands on the closed uid_w (deterministic, no reliance on bridge timing).
  coordinator()->RecordSettledUid(uid_w);
  coordinator()->RecordSettledUid(uid_a);
  coordinator()->StepBack();  // uid_a -> uid_w (closed WINDOW nested tab).
  coordinator()->Settle();

  EXPECT_TRUE(IsUidLiveInProfile(uid_w));  // the matched nested tab restored.
  EXPECT_FALSE(IsUidLiveInProfile(
      uid_other));  // its sibling did NOT (single-tab restore).
  // Prove the EXACT nested restore ran (not a fallback, which would ALSO
  // recreate only uid_w and leave uid_other closed): uid_w's nested restore
  // entry was CONSUMED while its sibling's remains (fallback never touches the
  // restore list).
  EXPECT_FALSE(RestoreEntryExistsForUid(uid_w));
  EXPECT_TRUE(RestoreEntryExistsForUid(uid_other));

  // uid_w's sidecar row rebound to closed=false.
  const std::vector<TabStateRow> states = ReadTabStates();
  const TabStateRow* row = FindState(states, uid_w);
  ASSERT_TRUE(row);
  EXPECT_FALSE(row->closed);
}

// reopen OFF (default): a closed uid is NOT reachable — Back lands on the older
// LIVE tab instead, and the closed tab is never reopened (today's behavior).
IN_PROC_BROWSER_TEST_F(RoamuxTabVisitReopenTest, ReopenOffSkipsClosed) {
  SeedThreeTabs();
  const std::string uid_b = UidAt(1);
  ASSERT_FALSE(uid_b.empty());

  tabs()->CloseWebContentsAt(1, TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
  ASSERT_EQ(-1, LiveIndexOfUid(uid_b));
  ASSERT_FALSE(
      browser()->profile()->GetPrefs()->GetBoolean(prefs::kReopenClosed));

  coordinator()->StepBack();  // C -> (B unreachable) -> A (older live tab).
  coordinator()->Settle();

  EXPECT_EQ(-1, LiveIndexOfUid(uid_b));  // B stays closed.
  EXPECT_EQ(UrlA(), tabs()->GetActiveWebContents()->GetLastCommittedURL());
}

}  // namespace
}  // namespace roamux

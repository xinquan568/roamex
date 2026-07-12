// SPDX-License-Identifier: Apache-2.0
// roam-23 (I-4.3) mandatory matrix (issue §6.3): a genuine user selection is
// committed; an API activation (ActivateTabAt kNone) is committed; the
// AUTO-SUCCESSOR selected when the active tab is closed is NEVER committed (the
// hard requirement); a window-close removal sequence causes no spurious commit;
// empty/NTP URLs are not recorded; and a disabled build commits nothing.
// (TDD: written RED before the bridge is wired.)

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_user_gesture_details.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "roamux/browser/tab_visit/settled_visit_journal_factory.h"
#include "roamux/browser/tab_visit/settled_visit_journal_service.h"
#include "roamux/browser/tab_visit/visits_store.h"
#include "roamux/common/roamux_features.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace roamux {
namespace {

using tab_visit::SettledVisitJournalFactory;
using tab_visit::VisitRow;

// Drains the SequenceBound-backed journal: RecordVisit (write) is posted before
// GetVisits (read) on the same store sequence, so the read reflects all prior
// writes. Returns the recorded URLs oldest-first.
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

bool Contains(const std::vector<std::string>& urls, const GURL& url) {
  return std::find(urls.begin(), urls.end(), url.spec()) != urls.end();
}

class RoamuxTabVisitObserverTest : public InProcessBrowserTest {
 public:
  RoamuxTabVisitObserverTest() {
    features_.InitAndEnableFeature(features::kTabVisitNav);
  }

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(embedded_test_server()->Start());
  }

 protected:
  GURL UrlA() { return embedded_test_server()->GetURL("/title1.html"); }
  GURL UrlB() { return embedded_test_server()->GetURL("/title2.html"); }

  TabStripModel* tabs() { return browser()->tab_strip_model(); }

  void ActivateExisting(int index,
                        TabStripUserGestureDetails::GestureType gesture) {
    tabs()->ActivateTabAt(index, TabStripUserGestureDetails(gesture));
  }

  base::test::ScopedFeatureList features_;
};

// A genuine user selection of an existing tab (kSelectionOnly) commits its URL.
IN_PROC_BROWSER_TEST_F(RoamuxTabVisitObserverTest, UserSelectionIsCommitted) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), UrlA()));
  ASSERT_TRUE(AddTabAtIndex(1, UrlB(), ui::PAGE_TRANSITION_TYPED));
  ASSERT_EQ(1, tabs()->active_index());  // The new foreground tab is active.

  ActivateExisting(0, TabStripUserGestureDetails::GestureType::kOther);

  EXPECT_TRUE(Contains(ReadVisitUrls(browser()->profile()), UrlA()));
}

// An API activation (ActivateTabAt with kNone -> CHANGE_REASON_NONE) is still a
// kSelectionOnly selection, so it commits (the adopted API policy, F1).
IN_PROC_BROWSER_TEST_F(RoamuxTabVisitObserverTest, ApiActivationIsCommitted) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), UrlA()));
  ASSERT_TRUE(AddTabAtIndex(1, UrlB(), ui::PAGE_TRANSITION_TYPED));

  ActivateExisting(0, TabStripUserGestureDetails::GestureType::kNone);

  EXPECT_TRUE(Contains(ReadVisitUrls(browser()->profile()), UrlA()));
}

// The HARD requirement: closing the active tab auto-selects a successor via a
// kRemoved change (reason NONE); that successor URL must NEVER be committed.
IN_PROC_BROWSER_TEST_F(RoamuxTabVisitObserverTest,
                       AutoSuccessorIsNeverCommitted) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), UrlA()));
  ASSERT_TRUE(AddTabAtIndex(1, UrlB(), ui::PAGE_TRANSITION_TYPED));
  ASSERT_EQ(1, tabs()->active_index());
  // Only kInserted has happened so far — nothing committed.
  ASSERT_FALSE(Contains(ReadVisitUrls(browser()->profile()), UrlA()));

  // Close the active tab (index 1); the model auto-activates tab 0 (UrlA).
  tabs()->CloseWebContentsAt(1, TabCloseTypes::CLOSE_USER_GESTURE);
  ASSERT_EQ(0, tabs()->active_index());

  EXPECT_FALSE(Contains(ReadVisitUrls(browser()->profile()), UrlA()));
}

// A window-close removal sequence (all kRemoved) causes no spurious commit.
IN_PROC_BROWSER_TEST_F(RoamuxTabVisitObserverTest,
                       WindowCloseNoSpuriousCommit) {
  Browser* second = CreateBrowser(browser()->profile());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(second, UrlA()));
  ASSERT_FALSE(Contains(ReadVisitUrls(browser()->profile()), UrlA()));

  CloseBrowserSynchronously(second);

  EXPECT_FALSE(Contains(ReadVisitUrls(browser()->profile()), UrlA()));
}

// Empty / about:blank / NTP URLs are gated out before RecordVisit (F2).
IN_PROC_BROWSER_TEST_F(RoamuxTabVisitObserverTest, EmptyAndNtpNotRecorded) {
  // Leave tab 0 on about:blank; add a real-URL foreground tab, then select the
  // about:blank tab — a kSelectionOnly activation of a non-recordable URL.
  ASSERT_TRUE(AddTabAtIndex(1, UrlB(), ui::PAGE_TRANSITION_TYPED));
  ActivateExisting(0, TabStripUserGestureDetails::GestureType::kOther);

  const std::vector<std::string> urls = ReadVisitUrls(browser()->profile());
  EXPECT_FALSE(Contains(urls, GURL("about:blank")));
  EXPECT_TRUE(urls.empty());
}

// Flag-off build: the bridge is inert and nothing is committed.
class RoamuxTabVisitObserverFlagOffTest : public InProcessBrowserTest {
 public:
  RoamuxTabVisitObserverFlagOffTest() {
    features_.InitAndDisableFeature(features::kTabVisitNav);
  }

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(embedded_test_server()->Start());
  }

 protected:
  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(RoamuxTabVisitObserverFlagOffTest,
                       NoCommitsWhenDisabled) {
  const GURL url_a = embedded_test_server()->GetURL("/title1.html");
  const GURL url_b = embedded_test_server()->GetURL("/title2.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url_a));
  ASSERT_TRUE(AddTabAtIndex(1, url_b, ui::PAGE_TRANSITION_TYPED));
  browser()->tab_strip_model()->ActivateTabAt(
      0, TabStripUserGestureDetails(
             TabStripUserGestureDetails::GestureType::kOther));

  EXPECT_TRUE(ReadVisitUrls(browser()->profile()).empty());
}

}  // namespace
}  // namespace roamux

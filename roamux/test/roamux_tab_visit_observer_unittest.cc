// SPDX-License-Identifier: Apache-2.0
// roam-23 (I-4.3): the pure commit decision. Tables every activation-change
// class to prove that ONLY a genuine kSelectionOnly active-tab change commits —
// covering the kReplaced (discard) and kRemoved (auto-successor / tear-off /
// window-close) shapes deterministically, independent of how hard they are to
// stage in a real browser (the browser-level AutoSuccessorIsNeverCommitted test
// is the end-to-end proof of the hard requirement).

#include "roamux/browser/tab_visit/tab_visit_activation_class.h"

#include "roamux/browser/tab_visit/visit_url_filter.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace roamux::tab_visit {
namespace {

TEST(TabVisitActivationClassTest, SelectionOnlyWithActiveChangeCommits) {
  EXPECT_TRUE(ShouldCommitActivation(TabActivationChange::kSelectionOnly,
                                     /*active_tab_changed=*/true));
}

TEST(TabVisitActivationClassTest,
     SelectionOnlyWithoutActiveChangeDoesNotCommit) {
  // A pure selection-model change that did not move the active tab.
  EXPECT_FALSE(ShouldCommitActivation(TabActivationChange::kSelectionOnly,
                                      /*active_tab_changed=*/false));
}

TEST(TabVisitActivationClassTest, RemovedNeverCommits) {
  // The auto-successor (close active tab), tear-off, and window-close all
  // arrive as kRemoved — even with active_tab_changed(), they must NOT commit.
  EXPECT_FALSE(ShouldCommitActivation(TabActivationChange::kRemoved,
                                      /*active_tab_changed=*/true));
  EXPECT_FALSE(ShouldCommitActivation(TabActivationChange::kRemoved,
                                      /*active_tab_changed=*/false));
}

TEST(TabVisitActivationClassTest, ReplacedNeverCommits) {
  // A discard/replacement: the tab did not change, its contents did.
  EXPECT_FALSE(ShouldCommitActivation(TabActivationChange::kReplaced,
                                      /*active_tab_changed=*/true));
  EXPECT_FALSE(ShouldCommitActivation(TabActivationChange::kReplaced,
                                      /*active_tab_changed=*/false));
}

TEST(TabVisitActivationClassTest, InsertedNeverCommits) {
  // A new foreground tab (its URL is the NTP; a real navigation is not an
  // activation).
  EXPECT_FALSE(ShouldCommitActivation(TabActivationChange::kInserted,
                                      /*active_tab_changed=*/true));
  EXPECT_FALSE(ShouldCommitActivation(TabActivationChange::kInserted,
                                      /*active_tab_changed=*/false));
}

TEST(TabVisitActivationClassTest, MovedNeverCommits) {
  EXPECT_FALSE(ShouldCommitActivation(TabActivationChange::kMoved,
                                      /*active_tab_changed=*/true));
  EXPECT_FALSE(ShouldCommitActivation(TabActivationChange::kMoved,
                                      /*active_tab_changed=*/false));
}

// The URL-gating branches (F2). Deterministic coverage of every non-recordable
// scheme/host the bridge relies on before RecordVisit.
TEST(VisitUrlFilterTest, EmptyAndInvalidNotRecordable) {
  EXPECT_FALSE(IsRecordableVisitUrl(GURL()));
  EXPECT_FALSE(IsRecordableVisitUrl(GURL("")));
  EXPECT_FALSE(IsRecordableVisitUrl(GURL("not a url")));
}

TEST(VisitUrlFilterTest, AboutBlankNotRecordable) {
  EXPECT_FALSE(IsRecordableVisitUrl(GURL("about:blank")));
  EXPECT_FALSE(IsRecordableVisitUrl(GURL("about:blank#frag")));
}

TEST(VisitUrlFilterTest, NewTabPageHostsNotRecordable) {
  EXPECT_FALSE(IsRecordableVisitUrl(GURL("chrome://newtab/")));
  EXPECT_FALSE(IsRecordableVisitUrl(GURL("chrome://new-tab-page/")));
  EXPECT_FALSE(
      IsRecordableVisitUrl(GURL("chrome://new-tab-page-third-party/")));
}

TEST(VisitUrlFilterTest, ChromeSearchSchemeNotRecordable) {
  EXPECT_FALSE(
      IsRecordableVisitUrl(GURL("chrome-search://local-ntp/local-ntp.html")));
  EXPECT_FALSE(
      IsRecordableVisitUrl(GURL("chrome-search://most-visited/title.html")));
}

TEST(VisitUrlFilterTest, RealPagesAreRecordable) {
  EXPECT_TRUE(IsRecordableVisitUrl(GURL("https://example.com/")));
  EXPECT_TRUE(IsRecordableVisitUrl(GURL("http://example.com/path?q=1")));
  // A non-NTP chrome:// page is a real destination and IS recorded.
  EXPECT_TRUE(IsRecordableVisitUrl(GURL("chrome://settings/")));
  EXPECT_TRUE(IsRecordableVisitUrl(GURL("chrome://history/")));
}

}  // namespace
}  // namespace roamux::tab_visit

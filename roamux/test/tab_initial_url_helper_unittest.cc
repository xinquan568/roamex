// SPDX-License-Identifier: Apache-2.0
// roam-11 (I-2.2): the capture rule (plan §4.7 / D1) — redirect-chain head,
// stickiness, lock semantics, initiation/transition gates, ignorable starts.
// (TDD: written RED before the implementation; runs in the
// ChromeRenderViewHostTestHarness + NavigationSimulator unit tier.)

#include "roamux/browser/tabs/tab_initial_url_helper.h"

#include "base/test/scoped_feature_list.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "content/public/test/navigation_simulator.h"
#include "content/public/test/test_renderer_host.h"
#include "roamux/common/roamux_features.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"

namespace roamux::tabs {
namespace {

class TabInitialUrlHelperTest : public ChromeRenderViewHostTestHarness {
 public:
  void SetUp() override {
    features_.InitAndEnableFeature(features::kInitialUrl);
    ChromeRenderViewHostTestHarness::SetUp();
    TabInitialUrlHelper::MaybeCreateForWebContents(web_contents());
  }

 protected:
  TabInitialUrlHelper* helper() {
    return TabInitialUrlHelper::FromWebContents(web_contents());
  }

  base::test::ScopedFeatureList features_;
};

TEST_F(TabInitialUrlHelperTest, CapturesRedirectChainHead) {
  auto simulator = content::NavigationSimulator::CreateBrowserInitiated(
      GURL("https://app.test/dashboard"), web_contents());
  simulator->Start();
  simulator->Redirect(GURL("https://idp.test/login"));
  simulator->Redirect(GURL("https://app.test/landing"));
  simulator->Commit();
  EXPECT_TRUE(helper()->has_initial_url());
  EXPECT_EQ(GURL("https://app.test/dashboard"), helper()->initial_url());
}

TEST_F(TabInitialUrlHelperTest, StickyAcrossLaterNavigations) {
  content::NavigationSimulator::NavigateAndCommitFromBrowser(
      web_contents(), GURL("https://first.test/"));
  content::NavigationSimulator::NavigateAndCommitFromBrowser(
      web_contents(), GURL("https://second.test/"));
  EXPECT_EQ(GURL("https://first.test/"), helper()->initial_url());
}

TEST_F(TabInitialUrlHelperTest, IgnorableStartThenRealNavigationCaptures) {
  content::NavigationSimulator::NavigateAndCommitFromBrowser(
      web_contents(), GURL("about:blank"));
  EXPECT_FALSE(helper()->has_initial_url());
  content::NavigationSimulator::NavigateAndCommitFromBrowser(
      web_contents(), GURL("https://real.test/"));
  EXPECT_EQ(GURL("https://real.test/"), helper()->initial_url());
}

TEST_F(TabInitialUrlHelperTest, ClientRedirectFirstNavigationRejected) {
  auto simulator = content::NavigationSimulator::CreateRendererInitiated(
      GURL("https://pushed.test/"), web_contents()->GetPrimaryMainFrame());
  simulator->SetTransition(ui::PageTransitionFromInt(
      ui::PAGE_TRANSITION_LINK | ui::PAGE_TRANSITION_CLIENT_REDIRECT));
  simulator->Commit();
  EXPECT_FALSE(helper()->has_initial_url());
}

TEST_F(TabInitialUrlHelperTest, FreshTabViaLinkAccepted) {
  // Renderer-initiated, but the tab's first-ever committed navigation: the
  // opener's click is the user intent.
  auto simulator = content::NavigationSimulator::CreateRendererInitiated(
      GURL("https://opened.test/"), web_contents()->GetPrimaryMainFrame());
  simulator->SetTransition(ui::PAGE_TRANSITION_LINK);
  simulator->Commit();
  EXPECT_TRUE(helper()->has_initial_url());
  EXPECT_EQ(GURL("https://opened.test/"), helper()->initial_url());
}

TEST_F(TabInitialUrlHelperTest, RendererNavigationAfterRealStartIsSticky) {
  content::NavigationSimulator::NavigateAndCommitFromBrowser(
      web_contents(), GURL("https://user.test/"));
  auto simulator = content::NavigationSimulator::CreateRendererInitiated(
      GURL("https://site-driven.test/"), web_contents()->GetPrimaryMainFrame());
  simulator->SetTransition(ui::PAGE_TRANSITION_LINK);
  simulator->Commit();
  EXPECT_EQ(GURL("https://user.test/"), helper()->initial_url());
}

TEST_F(TabInitialUrlHelperTest, SubframeNavigationIgnored) {
  content::NavigationSimulator::NavigateAndCommitFromBrowser(
      web_contents(), GURL("about:blank"));
  content::RenderFrameHost* subframe =
      content::RenderFrameHostTester::For(main_rfh())->AppendChild("child");
  content::NavigationSimulator::NavigateAndCommitFromDocument(
      GURL("https://subframe.test/"), subframe);
  EXPECT_FALSE(helper()->has_initial_url());
}

TEST_F(TabInitialUrlHelperTest, NtpStartIsIgnorable) {
  content::NavigationSimulator::NavigateAndCommitFromBrowser(
      web_contents(), GURL("chrome://newtab/"));
  EXPECT_FALSE(helper()->has_initial_url());
  content::NavigationSimulator::NavigateAndCommitFromBrowser(
      web_contents(), GURL("https://after-ntp.test/"));
  EXPECT_EQ(GURL("https://after-ntp.test/"), helper()->initial_url());
}

TEST_F(TabInitialUrlHelperTest, SameDocumentIgnored) {
  content::NavigationSimulator::NavigateAndCommitFromBrowser(
      web_contents(), GURL("about:blank"));
  auto simulator = content::NavigationSimulator::CreateRendererInitiated(
      GURL("about:blank#frag"), web_contents()->GetPrimaryMainFrame());
  simulator->CommitSameDocument();
  EXPECT_FALSE(helper()->has_initial_url());
}

TEST_F(TabInitialUrlHelperTest, UserSetLocksAgainstCapture) {
  helper()->SetUserInitialUrl(GURL("https://user-set.test/"));
  content::NavigationSimulator::NavigateAndCommitFromBrowser(
      web_contents(), GURL("https://navigated.test/"));
  EXPECT_TRUE(helper()->is_user_locked());
  EXPECT_EQ(GURL("https://user-set.test/"), helper()->initial_url());
}

TEST_F(TabInitialUrlHelperTest, RestoredValueBlocksNextCapture) {
  helper()->SetRestoredInitialUrl(GURL("https://restored.test/"));
  content::NavigationSimulator::NavigateAndCommitFromBrowser(
      web_contents(), GURL("https://restore-load.test/"));
  EXPECT_FALSE(helper()->is_user_locked());
  EXPECT_EQ(GURL("https://restored.test/"), helper()->initial_url());
}

class TabInitialUrlHelperFlagOffTest : public ChromeRenderViewHostTestHarness {
 public:
  void SetUp() override {
    features_.InitAndDisableFeature(features::kInitialUrl);
    ChromeRenderViewHostTestHarness::SetUp();
    TabInitialUrlHelper::MaybeCreateForWebContents(web_contents());
  }

 protected:
  base::test::ScopedFeatureList features_;
};

TEST_F(TabInitialUrlHelperFlagOffTest, NoHelperWhenFlagOff) {
  EXPECT_EQ(nullptr, TabInitialUrlHelper::FromWebContents(web_contents()));
}

}  // namespace
}  // namespace roamux::tabs

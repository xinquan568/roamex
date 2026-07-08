// SPDX-License-Identifier: Apache-2.0
// roam-11 (I-2.2) end-to-end: the §4.7 SSO scenario (initial_url is the
// redirect-chain head, not the IdP hop or the landing page), activation
// exclusions (prerender/BFCache never change the captured value), discard
// survival, and flag-off inertness.

#include "base/run_loop.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tab_list/tab_list_interface.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/prerender_test_util.h"
#include "net/dns/mock_host_resolver.h"
#include "roamex/browser/tabs/tab_initial_url_helper.h"
#include "roamex/common/roamex_features.h"
#include "roamex/test/support/sso_test_server.h"

namespace roamex {
namespace {

class RoamexInitialUrlTest : public InProcessBrowserTest {
 public:
  RoamexInitialUrlTest() {
    features_.InitAndEnableFeature(features::kInitialUrl);
  }

  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    InProcessBrowserTest::SetUpOnMainThread();
  }

 protected:
  content::WebContents* active_contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  tabs::TabInitialUrlHelper* helper() {
    return tabs::TabInitialUrlHelper::FromWebContents(active_contents());
  }

  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(RoamexInitialUrlTest, SsoRedirectChainCapturesHead) {
  test::SsoTestServer sso;
  ASSERT_TRUE(sso.Start());

  // app/dashboard -> 302 -> cross-origin IdP/login -> 302 -> app/landing.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), sso.dashboard_url()));
  EXPECT_EQ(sso.landing_url(), active_contents()->GetLastCommittedURL());

  ASSERT_NE(nullptr, helper());
  EXPECT_TRUE(helper()->has_initial_url());
  EXPECT_EQ(sso.dashboard_url(), helper()->initial_url())
      << "must record the chain head, not the IdP hop or the landing page";
}

IN_PROC_BROWSER_TEST_F(RoamexInitialUrlTest, BfCacheRestoreKeepsCapturedValue) {
  test::SsoTestServer sso;
  ASSERT_TRUE(sso.Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), sso.landing_url()));
  const GURL captured = helper()->initial_url();
  EXPECT_EQ(sso.landing_url(), captured);

  // Cross-origin away and back (BFCache-eligible).
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), sso.dashboard_url().Resolve("/landing")));
  active_contents()->GetController().GoBack();
  ASSERT_TRUE(content::WaitForLoadStop(active_contents()));

  EXPECT_EQ(captured, helper()->initial_url())
      << "a BFCache restore must not change the captured value";
}

IN_PROC_BROWSER_TEST_F(RoamexInitialUrlTest, DiscardKeepsCapturedValue) {
  test::SsoTestServer sso;
  ASSERT_TRUE(sso.Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), sso.landing_url(), WindowOpenDisposition::NEW_BACKGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  content::WebContents* background =
      browser()->tab_strip_model()->GetWebContentsAt(1);
  tabs::TabInitialUrlHelper* bg_helper =
      tabs::TabInitialUrlHelper::FromWebContents(background);
  ASSERT_NE(nullptr, bg_helper);
  const GURL captured = bg_helper->initial_url();
  ASSERT_TRUE(captured.is_valid());

  TabListInterface* tab_list = TabListInterface::From(browser());
  ASSERT_NE(nullptr, tab_list->DiscardTab(tab_list->GetTab(1)->GetHandle()));
  base::RunLoop().RunUntilIdle();

  tabs::TabInitialUrlHelper* new_helper =
      tabs::TabInitialUrlHelper::FromWebContents(
          browser()->tab_strip_model()->GetWebContentsAt(1));
  ASSERT_NE(nullptr, new_helper);
  EXPECT_EQ(captured, new_helper->initial_url());
  EXPECT_TRUE(new_helper->has_initial_url());
}

class RoamexInitialUrlPrerenderTest : public RoamexInitialUrlTest {
 public:
  RoamexInitialUrlPrerenderTest()
      : prerender_helper_(base::BindRepeating(
            &RoamexInitialUrlPrerenderTest::GetActiveWebContents,
            base::Unretained(this))) {}

  void SetUp() override {
    prerender_helper_.RegisterServerRequestMonitor(embedded_test_server());
    RoamexInitialUrlTest::SetUp();
  }

  void SetUpOnMainThread() override {
    RoamexInitialUrlTest::SetUpOnMainThread();
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  content::WebContents* GetActiveWebContents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

 protected:
  content::test::PrerenderTestHelper prerender_helper_;
};

IN_PROC_BROWSER_TEST_F(RoamexInitialUrlPrerenderTest,
                       PrerenderActivationKeepsCapturedValue) {
  const GURL initial = embedded_test_server()->GetURL("/empty.html");
  const GURL prerender = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), initial));
  EXPECT_EQ(initial, helper()->initial_url());

  prerender_helper_.AddPrerender(prerender);
  prerender_helper_.NavigatePrimaryPage(prerender);  // Activation.

  EXPECT_EQ(initial, helper()->initial_url())
      << "a prerender activation must not change the captured value";
}

class RoamexInitialUrlFlagOffTest : public InProcessBrowserTest {
 public:
  RoamexInitialUrlFlagOffTest() {
    features_.InitAndDisableFeature(features::kInitialUrl);
  }

 protected:
  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(RoamexInitialUrlFlagOffTest, NoHelperWhenFlagOff) {
  EXPECT_EQ(nullptr, tabs::TabInitialUrlHelper::FromWebContents(
                         browser()->tab_strip_model()->GetActiveWebContents()));
}

}  // namespace
}  // namespace roamex

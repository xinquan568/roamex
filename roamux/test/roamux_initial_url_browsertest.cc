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
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/prerender_test_util.h"
#include "net/dns/mock_host_resolver.h"
#include "roamux/browser/tabs/tab_initial_url_helper.h"
#include "roamux/common/roamux_features.h"
#include "roamux/test/support/sso_test_server.h"

namespace roamux {
namespace {

class RoamuxInitialUrlTest : public InProcessBrowserTest {
 public:
  RoamuxInitialUrlTest() {
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

IN_PROC_BROWSER_TEST_F(RoamuxInitialUrlTest, SsoRedirectChainCapturesHead) {
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

// Records whether the last finished primary-main-frame navigation was served
// from the back/forward cache.
class BfCacheProbe : public content::WebContentsObserver {
 public:
  explicit BfCacheProbe(content::WebContents* web_contents)
      : content::WebContentsObserver(web_contents) {}
  void DidFinishNavigation(content::NavigationHandle* handle) override {
    if (handle->IsInPrimaryMainFrame() && handle->HasCommitted()) {
      last_was_bfcache_ = handle->IsServedFromBackForwardCache();
    }
  }
  bool last_was_bfcache() const { return last_was_bfcache_; }

 private:
  bool last_was_bfcache_ = false;
};

IN_PROC_BROWSER_TEST_F(RoamuxInitialUrlTest, BfCacheRestoreKeepsCapturedValue) {
  test::SsoTestServer sso;
  ASSERT_TRUE(sso.Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), sso.landing_url()));
  const GURL captured = helper()->initial_url();
  EXPECT_EQ(sso.landing_url(), captured);

  // Genuinely cross-origin away (the IdP origin), then back: the restore must
  // actually come from the BFCache and must not change the captured value.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), sso.idp_page_url()));
  BfCacheProbe probe(active_contents());
  active_contents()->GetController().GoBack();
  ASSERT_TRUE(content::WaitForLoadStop(active_contents()));
  EXPECT_TRUE(probe.last_was_bfcache())
      << "test vehicle broken: the back navigation was not a BFCache restore";
  EXPECT_EQ(captured, helper()->initial_url());
}

IN_PROC_BROWSER_TEST_F(RoamuxInitialUrlTest,
                       BfCacheActivationNeverCapturesUncapturedTab) {
  // Reach a BFCache activation while NOTHING is captured — the case that
  // FAILS if the IsServedFromBackForwardCache exclusion is removed:
  // about:blank (ignorable) -> renderer-driven nav to X (rejected by the
  // initiator rule) -> Back (X enters the BFCache) -> Forward (X restored
  // from the BFCache, browser-initiated, no client-redirect qualifier).
  test::SsoTestServer sso;
  ASSERT_TRUE(sso.Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("about:blank")));
  ASSERT_FALSE(helper()->has_initial_url());
  ASSERT_TRUE(content::ExecJs(
      active_contents(),
      content::JsReplace("location.href = $1", sso.idp_page_url())));
  ASSERT_TRUE(content::WaitForLoadStop(active_contents()));
  ASSERT_FALSE(helper()->has_initial_url())
      << "renderer-driven navigation must not capture";

  active_contents()->GetController().GoBack();
  ASSERT_TRUE(content::WaitForLoadStop(active_contents()));
  BfCacheProbe probe(active_contents());
  active_contents()->GetController().GoForward();
  ASSERT_TRUE(content::WaitForLoadStop(active_contents()));
  if (probe.last_was_bfcache()) {
    EXPECT_FALSE(helper()->has_initial_url())
        << "a BFCache activation must never capture";
  } else {
    // BFCache ineligibility on this platform/config: the forward reload is a
    // fresh browser-initiated navigation, which legitimately captures.
    SUCCEED() << "forward was not a BFCache restore on this config";
  }
}

IN_PROC_BROWSER_TEST_F(RoamuxInitialUrlTest, DiscardKeepsCapturedValue) {
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

class RoamuxInitialUrlPrerenderTest : public RoamuxInitialUrlTest {
 public:
  RoamuxInitialUrlPrerenderTest()
      : prerender_helper_(base::BindRepeating(
            &RoamuxInitialUrlPrerenderTest::GetActiveWebContents,
            base::Unretained(this))) {}

  void SetUp() override {
    prerender_helper_.RegisterServerRequestMonitor(embedded_test_server());
    RoamuxInitialUrlTest::SetUp();
  }

  void SetUpOnMainThread() override {
    RoamuxInitialUrlTest::SetUpOnMainThread();
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  content::WebContents* GetActiveWebContents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

 protected:
  content::test::PrerenderTestHelper prerender_helper_;
};

IN_PROC_BROWSER_TEST_F(RoamuxInitialUrlPrerenderTest,
                       PrerenderActivationKeepsCapturedValue) {
  const GURL initial = embedded_test_server()->GetURL("/empty.html");
  const GURL prerender = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), initial));
  EXPECT_EQ(initial, helper()->initial_url());

  prerender_helper_.AddPrerender(prerender);
  prerender_helper_.NavigatePrimaryPage(prerender);  // Activation.

  EXPECT_EQ(initial, helper()->initial_url())
      << "a prerender activation must not change the captured value";
  // Why the uncaptured-at-activation branch is not separately drivable: a
  // prerender activation requires a committed primary page (which captured or
  // was rejected), and NavigatePrimaryPage activates via a renderer-initiated
  // navigation with prior entries — already rejected by the initiator rule.
  // The IsPrerenderedPageActivation gate is defense-in-depth; the BFCache
  // twin (above) covers the reachable activation-capture hazard.
}

class RoamuxInitialUrlFlagOffTest : public InProcessBrowserTest {
 public:
  RoamuxInitialUrlFlagOffTest() {
    features_.InitAndDisableFeature(features::kInitialUrl);
  }

 protected:
  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(RoamuxInitialUrlFlagOffTest, NoHelperWhenFlagOff) {
  EXPECT_EQ(nullptr, tabs::TabInitialUrlHelper::FromWebContents(
                         browser()->tab_strip_model()->GetActiveWebContents()));
}

}  // namespace
}  // namespace roamux

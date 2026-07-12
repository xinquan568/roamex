// SPDX-License-Identifier: Apache-2.0
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "components/webui/chrome_urls/pref_names.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/page_type.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "roamux/common/roamux_features.h"
#include "roamux/common/roamux_url_constants.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace roamux {
namespace {

// roam-91: end-to-end roamux:// alias behavior (patch 0028 + the flag-gated
// rewrite in //roamux/browser/scheme), including the two security proofs from
// the Phase-1 analysis: renderer-initiated navigations stay blocked, and
// unrewritten roamux:// URLs are dropped without commit (never the OS
// external-protocol path, never a shadowed chrome:// host).
//
// Assertion vocabulary (deliberate): WebContents::GetLastCommittedURL() and
// the 1-arg NavigateToURL success check observe the entry's VIRTUAL URL (the
// typed roamux:// form, preserved by the entry-creation rewrite), while
// NavigationEntry::GetURL() observes the real committed URL — the tests below
// assert BOTH sides explicitly.
class RoamuxSchemeAliasBrowserTest : public InProcessBrowserTest {
public:
  RoamuxSchemeAliasBrowserTest() {
    features_.InitAndEnableFeature(features::kRoamuxSchemeAlias);
  }

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    // chrome://roamux-about is registered internal-only
    // (DefaultInternalWebUIConfig); without this pref HandleWebUI diverts it
    // to chrome://debug-webuis-disabled (same enablement roam-37's mocha
    // browsertest performs).
    g_browser_process->local_state()->SetBoolean(
        chrome_urls::kInternalOnlyUisEnabled, true);
  }

protected:
  content::WebContents *web_contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  content::NavigationEntry *last_entry() {
    return web_contents()->GetController().GetLastCommittedEntry();
  }

  base::test::ScopedFeatureList features_;
};

// AC1: a browser-initiated roamux://about commits chrome://roamux-about while
// the omnibox-visible virtual URL stays roamux://about.
IN_PROC_BROWSER_TEST_F(RoamuxSchemeAliasBrowserTest,
                       AboutAliasLoadsAboutWebUI) {
  // 1-arg NavigateToURL succeeds iff the load is non-error and the observed
  // (virtual) URL equals the requested URL — i.e. the alias loads AND keeps
  // displaying as roamux://about.
  EXPECT_TRUE(content::NavigateToURL(web_contents(), GURL("roamux://about")));
  content::NavigationEntry *entry = last_entry();
  ASSERT_TRUE(entry);
  EXPECT_EQ(content::PAGE_TYPE_NORMAL, entry->GetPageType());
  EXPECT_EQ(GURL("chrome://roamux-about/"), entry->GetURL());
  EXPECT_EQ(GURL("roamux://about"), entry->GetVirtualURL());
}

// AC2: roamux://flags aliases to the upstream flags surface.
IN_PROC_BROWSER_TEST_F(RoamuxSchemeAliasBrowserTest,
                       FlagsAliasLoadsChromeFlags) {
  EXPECT_TRUE(content::NavigateToURL(web_contents(), GURL("roamux://flags")));
  content::NavigationEntry *entry = last_entry();
  ASSERT_TRUE(entry);
  EXPECT_EQ(content::PAGE_TYPE_NORMAL, entry->GetPageType());
  EXPECT_EQ(GURL("chrome://flags/"), entry->GetURL());
  EXPECT_EQ(GURL("roamux://flags"), entry->GetVirtualURL());
}

// AC3: the alias target keeps working directly — committed URL asserted (not
// just the virtual URL, which would mask an interstitial diversion). Also the
// browser-level check that about.html's <title> carries the Roamux brand
// (roam-93: the mocha harness cannot see about.html's title, so it is pinned
// here at the real page load).
IN_PROC_BROWSER_TEST_F(RoamuxSchemeAliasBrowserTest,
                       DirectChromeHostUnaffected) {
  EXPECT_TRUE(
      content::NavigateToURL(web_contents(), GURL("chrome://roamux-about/")));
  content::NavigationEntry *entry = last_entry();
  ASSERT_TRUE(entry);
  EXPECT_EQ(content::PAGE_TYPE_NORMAL, entry->GetPageType());
  EXPECT_EQ(GURL("chrome://roamux-about/"), entry->GetURL());
  EXPECT_EQ(u"About Roamux", web_contents()->GetTitle());
}

// AC3: unmapped roamux:// hosts never alias into the chrome:// namespace —
// the curated map is not a generic roamux://X → chrome://X rewrite. A
// handled-but-unmapped roamux:// URL is dropped without committing anything:
// the tab stays on the initial about:blank — never chrome://settings, never
// an OS external-protocol handoff.
IN_PROC_BROWSER_TEST_F(RoamuxSchemeAliasBrowserTest, UnmappedHostDoesNotAlias) {
  EXPECT_FALSE(
      content::NavigateToURL(web_contents(), GURL("roamux://settings")));
  content::NavigationEntry *entry = last_entry();
  ASSERT_TRUE(entry);
  EXPECT_EQ(GURL("about:blank"), entry->GetURL());
  EXPECT_NE(GURL("chrome://settings/"), entry->GetURL());
}

// AC4 (D2a proof): web content cannot reach the About WebUI through the alias.
// "roamux" is listed in ProfileIOData::IsHandledProtocol, so CanRequestURL
// denies it to renderers — the same layer that blocks chrome:// links from
// web pages.
IN_PROC_BROWSER_TEST_F(RoamuxSchemeAliasBrowserTest,
                       RendererInitiatedIsBlocked) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(content::NavigateToURL(
      web_contents(), embedded_test_server()->GetURL("/title1.html")));
  EXPECT_FALSE(content::NavigateToURLFromRenderer(web_contents(),
                                                  GURL("roamux://about")));
  content::NavigationEntry *entry = last_entry();
  ASSERT_TRUE(entry);
  EXPECT_NE(GURL("chrome://roamux-about/"), entry->GetURL());
}

// AC4 (D7 proof): redirects never alias — forward rewriting happens at
// navigation-entry creation only, so a server redirect targeting roamux://
// lands on the handled-scheme dead-end, not the About WebUI.
IN_PROC_BROWSER_TEST_F(RoamuxSchemeAliasBrowserTest,
                       ServerRedirectDoesNotAlias) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL redirect =
      embedded_test_server()->GetURL("/server-redirect?roamux://about");
  EXPECT_FALSE(content::NavigateToURL(web_contents(), redirect));
  content::NavigationEntry *entry = last_entry();
  ASSERT_TRUE(entry);
  EXPECT_NE(GURL("chrome://roamux-about/"), entry->GetURL());
}

// AC4 (D7 proof, renderer-initiated): web content driving the same server
// redirect exercises the distinct renderer redirect path (CanRedirectToURL,
// then the later CanRequestURL screening) — it must not reach the WebUI
// either.
IN_PROC_BROWSER_TEST_F(RoamuxSchemeAliasBrowserTest,
                       ServerRedirectFromRendererDoesNotAlias) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(content::NavigateToURL(
      web_contents(), embedded_test_server()->GetURL("/title1.html")));
  const GURL redirect =
      embedded_test_server()->GetURL("/server-redirect?roamux://about");
  EXPECT_FALSE(content::NavigateToURLFromRenderer(web_contents(), redirect));
  content::NavigationEntry *entry = last_entry();
  ASSERT_TRUE(entry);
  EXPECT_NE(GURL("chrome://roamux-about/"), entry->GetURL());
}

// AC5 (D2b proof): flag-off is inert — no rewrite; the handled-scheme
// navigation is dropped without commit (tab stays on about:blank) rather than
// reaching the OS external-protocol path (P3: the feature ships disabled).
// Internal UIs are enabled here too, so a rewrite bug (rather than the
// internal-UI gate) would be what a chrome://roamux-about commit indicates.
class RoamuxSchemeAliasFlagOffBrowserTest : public InProcessBrowserTest {
public:
  RoamuxSchemeAliasFlagOffBrowserTest() {
    features_.InitAndDisableFeature(features::kRoamuxSchemeAlias);
  }

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    g_browser_process->local_state()->SetBoolean(
        chrome_urls::kInternalOnlyUisEnabled, true);
  }

protected:
  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(RoamuxSchemeAliasFlagOffBrowserTest, FlagOffIsInert) {
  content::WebContents *wc =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_FALSE(content::NavigateToURL(wc, GURL("roamux://about")));
  content::NavigationEntry *entry = wc->GetController().GetLastCommittedEntry();
  ASSERT_TRUE(entry);
  EXPECT_EQ(GURL("about:blank"), entry->GetURL());
  EXPECT_NE(GURL("chrome://roamux-about/"), entry->GetURL());
}

// roam-93 dieback (D2b): the OLD roamex:// scheme (pre-rebrand) is dead — it
// never reaches the About WebUI (back to unknown-scheme behavior; exact error
// surface not asserted). The "roamex" literal is historical and stays
// (roam-94).
IN_PROC_BROWSER_TEST_F(RoamuxSchemeAliasBrowserTest, OldRoamexSchemeIsDead) {
  EXPECT_FALSE(content::NavigateToURL(web_contents(), GURL("roamex://about")));
  content::NavigationEntry *entry = last_entry();
  ASSERT_TRUE(entry);
  EXPECT_NE(GURL("chrome://roamux-about/"), entry->GetURL());
}

} // namespace
} // namespace roamux

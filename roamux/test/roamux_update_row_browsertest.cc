// SPDX-License-Identifier: Apache-2.0
// roam-160: the NATIVE chrome://settings/help update row, driven end-to-end
// through the production chain minus live Sparkle: synthetic UpdateEvents are
// injected at RoamuxUpdateService::OnUpdateEvent (the conformer's entry
// point), flow through the state machine -> snapshot subscription ->
// RoamuxVersionUpdater -> AboutHandler's StatusCallback -> WebUI event ->
// the about-page DOM, which this test asserts per state (message, icon,
// button visibility — the issue's explicit criterion). Also covered: the
// consent gate (rendering an offer downloads nothing), an end-to-end Skip
// dispatch proof (a skipped version never re-surfaces as an offer — the click
// must reach the service's state machine to have that effect), the
// signature-failure no-retry copy, and the branded-page support.google.com
// ban. Runs in roamux_browsertests => tier-2's Roamux* filter (the step-6 F1
// re-homing away from the CI-orphaned mocha path).

#include <string>

#include "base/strings/stringprintf.h"
#include "base/test/run_until.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "roamux/browser/updates/roamux_update_service.h"
#include "roamux/browser/updates/roamux_update_service_factory.h"
#include "roamux/browser/updates/update_state_machine.h"
#include "roamux/test/support/roamux_browser_test.h"
#include "url/gurl.h"

namespace roamux::updates {
namespace {

// Pierces the settings shadow chain to the about page. Kept as a JS prelude
// every snippet shares.
constexpr char kPrelude[] = R"(
    function aboutPage() {
      const ui = document.querySelector('settings-ui');
      if (!ui || !ui.shadowRoot) return null;
      const main = ui.shadowRoot.querySelector('settings-main');
      if (!main || !main.shadowRoot) return null;
      return main.shadowRoot.querySelector('settings-about-page');
    }
    function inAbout(sel) {
      const page = aboutPage();
      return page && page.shadowRoot ?
          page.shadowRoot.querySelector(sel) : null;
    }
    function visible(el) {
      return !!el && !el.hidden && el.offsetParent !== null;
    }
    function statusText() {
      const el = inAbout('#updateStatusMessage');
      return el ? el.textContent.trim() : '';
    }
)";

std::string Poll(const std::string& condition) {
  return base::StringPrintf(
      R"((function() { %s
           return new Promise(resolve => {
             const t0 = Date.now();
             (function poll() {
               let ok = false;
               try { ok = !!(%s); } catch (e) {}
               if (ok) return resolve(true);
               if (Date.now() - t0 > 8000) return resolve(false);
               setTimeout(poll, 50);
             })();
           });
         })())",
      kPrelude, condition.c_str());
}

}  // namespace

class RoamuxUpdateRowBrowserTest : public roamux::test::RoamuxBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    roamux::test::RoamuxBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(),
                                             GURL("chrome://settings/help")));
    service_ = RoamuxUpdateServiceFactory::GetForProfile(browser()->profile());
    ASSERT_NE(nullptr, service_);
    web_contents_ = browser()->tab_strip_model()->GetActiveWebContents();
    // The page's connectedCallback fires refreshUpdateStatus (auto-check);
    // wait for the about page to exist before injecting states.
    ASSERT_TRUE(Reached("aboutPage() && aboutPage().shadowRoot"));
  }

  void Fire(UpdateEventType type,
            const std::string& version = "",
            const std::string& error = "",
            int64_t received = 0,
            int64_t total = 0) {
    UpdateEvent event;
    event.type = type;
    event.version = version;
    event.error = error;
    event.received = received;
    event.total = total;
    service_->OnUpdateEvent(event);
  }

  [[nodiscard]] bool Reached(const std::string& condition) {
    return content::EvalJs(web_contents_, Poll(condition)).ExtractBool();
  }

  raw_ptr<RoamuxUpdateService, DanglingUntriaged> service_ = nullptr;
  raw_ptr<content::WebContents, DanglingUntriaged> web_contents_ = nullptr;
};

IN_PROC_BROWSER_TEST_F(RoamuxUpdateRowBrowserTest, CheckingRendersThrobber) {
  Fire(UpdateEventType::kCheckStarted);
  EXPECT_TRUE(Reached("statusText().length > 0"));
  EXPECT_TRUE(Reached("visible(inAbout('#throbber'))"));
}

IN_PROC_BROWSER_TEST_F(RoamuxUpdateRowBrowserTest, UpToDateRendersUpdated) {
  Fire(UpdateEventType::kCheckStarted);
  Fire(UpdateEventType::kUpToDate);
  EXPECT_TRUE(Reached("statusText().toLowerCase().includes('up to date')"));
  EXPECT_TRUE(
      Reached("!visible(inAbout('#roamuxDownload')) &&"
              " !visible(inAbout('#relaunch'))"));
}

IN_PROC_BROWSER_TEST_F(RoamuxUpdateRowBrowserTest,
                       OfferRendersDownloadAndSkipAndDownloadsNothing) {
  Fire(UpdateEventType::kCheckStarted);
  Fire(UpdateEventType::kUpdateFound, "9.9.9-test");
  EXPECT_TRUE(Reached("statusText().includes('9.9.9-test')"));
  EXPECT_TRUE(
      Reached("visible(inAbout('#roamuxDownload')) &&"
              " visible(inAbout('#roamuxSkip'))"));
  EXPECT_TRUE(
      Reached("!visible(inAbout('#relaunch')) &&"
              " !visible(inAbout('#roamuxTryAgain'))"));
  // Consent gate: rendering the offer fires no download — the row must still
  // show the offer (not a progress state) after settling.
  EXPECT_TRUE(Reached("statusText().includes('9.9.9-test')"));
  EXPECT_EQ(UpdateStatus::kAvailable, service_->snapshot_for_testing().status);
}

IN_PROC_BROWSER_TEST_F(RoamuxUpdateRowBrowserTest, DownloadingRendersPercent) {
  Fire(UpdateEventType::kCheckStarted);
  Fire(UpdateEventType::kUpdateFound, "9.9.9-test");
  Fire(UpdateEventType::kDownloadStarted);
  Fire(UpdateEventType::kDownloadProgress, "", "", 42, 100);
  EXPECT_TRUE(Reached("statusText().includes('42%')"));
  EXPECT_TRUE(Reached("visible(inAbout('#throbber'))"));
}

IN_PROC_BROWSER_TEST_F(RoamuxUpdateRowBrowserTest, ReadyRendersRelaunch) {
  Fire(UpdateEventType::kCheckStarted);
  Fire(UpdateEventType::kUpdateFound, "9.9.9-test");
  Fire(UpdateEventType::kDownloadStarted);
  Fire(UpdateEventType::kReadyToInstall);
  EXPECT_TRUE(Reached("statusText().toLowerCase().includes('relaunch')"));
  EXPECT_TRUE(Reached("visible(inAbout('#relaunch'))"));
  EXPECT_TRUE(Reached("!visible(inAbout('#roamuxDownload'))"));
}

IN_PROC_BROWSER_TEST_F(RoamuxUpdateRowBrowserTest,
                       CheckFailureRendersTryAgainAndDetail) {
  Fire(UpdateEventType::kCheckStarted);
  Fire(UpdateEventType::kError, "",
       "the appcast could not be loaded"
       " (SUAppcastError 2001)");
  EXPECT_TRUE(Reached("statusText().includes(\"Couldn't check for updates\")"));
  EXPECT_TRUE(Reached("visible(inAbout('#roamuxTryAgain'))"));
  // The dimmed alpha-only raw line carries the Sparkle detail.
  EXPECT_TRUE(
      Reached("inAbout('#roamuxUpdateDetail') &&"
              " inAbout('#roamuxUpdateDetail').textContent"
              ".includes('SUAppcastError')"));
  // cr:error is the FAILED icon (the enum-collapse gotcha).
  EXPECT_TRUE(
      Reached("inAbout('.icon-container cr-icon') &&"
              " inAbout('.icon-container cr-icon').getAttribute('icon') === "
              "'cr:error'"));
  // The branded page renders NO support.google.com link, and the
  // obsolete-system link's target is repointed at the repo docs even while
  // hidden (step-8 — the criterion is about rendering; the update-error
  // learn-more anchor keeps its upstream href but is branded-hidden).
  EXPECT_TRUE(
      Reached("Array.from(aboutPage().shadowRoot.querySelectorAll('a'))"
              ".every(a => !visible(a) ||"
              " !a.href.includes('support.google.com'))"));
  EXPECT_TRUE(
      Reached("!inAbout('#deprecationWarning a') ||"
              " inAbout('#deprecationWarning a').href.includes("
              "'github.com/xinquan568/roamux')"));
}

IN_PROC_BROWSER_TEST_F(RoamuxUpdateRowBrowserTest,
                       SignatureFailureOffersNoRetry) {
  Fire(UpdateEventType::kCheckStarted);
  Fire(UpdateEventType::kError, "",
       "the update is improperly signed and could not be validated"
       " (SUSignatureError 4001)");
  EXPECT_TRUE(Reached("statusText().includes(\"couldn't be verified\")"));
  EXPECT_TRUE(Reached("!visible(inAbout('#roamuxTryAgain'))"));
}

// End-to-end dispatch proof for Skip: only a click that actually reaches
// RoamuxUpdateService::Skip() (through AboutHandler's roamuxSkipUpdate) makes
// the state machine refuse to re-offer the version.
IN_PROC_BROWSER_TEST_F(RoamuxUpdateRowBrowserTest, SkipClickReachesTheService) {
  Fire(UpdateEventType::kCheckStarted);
  Fire(UpdateEventType::kUpdateFound, "9.9.9-test");
  ASSERT_TRUE(Reached("visible(inAbout('#roamuxSkip'))"));
  ASSERT_TRUE(content::ExecJs(
      web_contents_,
      std::string(kPrelude) + "inAbout('#roamuxSkip').click();"));
  // The click round-trips renderer -> AboutHandler -> service; wait for the
  // state machine to record the skip before re-offering.
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return service_->skipped_version_for_testing() == "9.9.9-test";
  }));
  Fire(UpdateEventType::kCheckStarted);
  Fire(UpdateEventType::kUpdateFound, "9.9.9-test");
  EXPECT_TRUE(Reached("!statusText().includes('9.9.9-test')"))
      << "a skipped version must never re-surface as an offer";
}

// Step-8 F2: every consent/recovery button's click must reach its service
// verb — proven via the service's dispatch counters (the owner's Sparkle
// replies are nil in tests, so the counters are the observable effect).
IN_PROC_BROWSER_TEST_F(RoamuxUpdateRowBrowserTest,
                       DownloadClickReachesTheService) {
  Fire(UpdateEventType::kCheckStarted);
  Fire(UpdateEventType::kUpdateFound, "9.9.9-test");
  ASSERT_TRUE(Reached("visible(inAbout('#roamuxDownload'))"));
  const int before = service_->downloads_for_testing();
  ASSERT_TRUE(content::ExecJs(
      web_contents_,
      std::string(kPrelude) + "inAbout('#roamuxDownload').click();"));
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return service_->downloads_for_testing() == before + 1; }));
}

IN_PROC_BROWSER_TEST_F(RoamuxUpdateRowBrowserTest,
                       TryAgainClickReachesTheService) {
  Fire(UpdateEventType::kCheckStarted);
  Fire(UpdateEventType::kError, "", "SUAppcastError 2001");
  ASSERT_TRUE(Reached("visible(inAbout('#roamuxTryAgain'))"));
  const int before = service_->checks_for_testing();
  ASSERT_TRUE(content::ExecJs(
      web_contents_,
      std::string(kPrelude) + "inAbout('#roamuxTryAgain').click();"));
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return service_->checks_for_testing() > before; }));
}

IN_PROC_BROWSER_TEST_F(RoamuxUpdateRowBrowserTest,
                       RelaunchClickRoutesThroughSparkle) {
  Fire(UpdateEventType::kCheckStarted);
  Fire(UpdateEventType::kUpdateFound, "9.9.9-test");
  Fire(UpdateEventType::kDownloadStarted);
  Fire(UpdateEventType::kReadyToInstall);
  ASSERT_TRUE(Reached("visible(inAbout('#relaunch'))"));
  const int before = service_->relaunches_for_testing();
  ASSERT_TRUE(content::ExecJs(
      web_contents_, std::string(kPrelude) + "inAbout('#relaunch').click();"));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return service_->relaunches_for_testing() == before + 1;
  })) << "branded NEARLY_UPDATED Relaunch must route InstallAndRelaunch"
         " (install-on-quit), not the generic relaunch";
}

// roam-161: the "Get help with Roamux" row must open the Roamux help doc in a
// NEW tab (pending-entry assert — no dependence on a real GitHub load), not
// Google's Chrome support site.
IN_PROC_BROWSER_TEST_F(RoamuxUpdateRowBrowserTest, HelpRowOpensRoamuxDocs) {
  ui_test_utils::TabAddedWaiter tab_added(browser());
  ASSERT_TRUE(content::ExecJs(
      web_contents_, std::string(kPrelude) + "inAbout('#help').click();"));
  content::WebContents* new_tab = tab_added.Wait();
  ASSERT_NE(nullptr, new_tab);
  // The tab is inserted before its navigation starts; poll the pending-entry
  // (visible) URL — no dependence on the remote page actually loading.
  const GURL expected(
      "https://github.com/xinquan568/roamux/blob/main/docs/help.md");
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return new_tab->GetVisibleURL() == expected;
  })) << "the branded help row must open the Roamux help doc, not Google's"
         " support site (visible: "
      << new_tab->GetVisibleURL() << ")";
}

}  // namespace roamux::updates

// SPDX-License-Identifier: Apache-2.0
// roam-140: the LIVE settings/help update path (complements the TS mocha suites,
// which exercise <roamux-update-card> against a fake handler):
//   * factory ProfileSelection — a regular profile has a RoamuxUpdateService,
//     an OTR profile does NOT (kOriginalOnly, not kRedirectedToOriginal), so the
//     branded update card degrades (updatesAvailable=false) off the record; and
//   * the real chrome://settings/help commits WITHOUT a bad-message renderer
//     kill, proving the SettingsUI UpdatePageHandlerFactory binder is registered
//     (a missing binder is the roam-138 failure mode) and exposes
//     updatesAvailable=true for a regular profile.
// This file only compiles under roamux_enable_sparkle (the service target).

#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_test_util.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "roamux/browser/updates/roamux_update_service.h"
#include "roamux/browser/updates/roamux_update_service_factory.h"
#include "url/gurl.h"

namespace roamux::updates {
namespace {

using RoamuxUpdateServiceFactoryBrowserTest = InProcessBrowserTest;

// Finding 2: regular profiles get a service; OTR does NOT — so updatesAvailable
// is false off the record and the update card degrades there.
IN_PROC_BROWSER_TEST_F(RoamuxUpdateServiceFactoryBrowserTest,
                       RegularHasServiceOtrDoesNot) {
  Profile* regular = browser()->profile();
  EXPECT_NE(nullptr, RoamuxUpdateServiceFactory::GetForProfile(regular));

  Profile* otr = regular->GetPrimaryOTRProfile(/*create_if_needed=*/true);
  EXPECT_EQ(nullptr, RoamuxUpdateServiceFactory::GetForProfile(otr));
}

// Finding 3 (multi-sink): two regular profiles get DISTINCT services, each of
// which independently subscribes its own sink to the one process-wide
// SparkleOwner (base::RepeatingCallbackList broadcast) — so neither facade is
// starved by the other (the old single re-pointed sink was last-writer-wins).
IN_PROC_BROWSER_TEST_F(RoamuxUpdateServiceFactoryBrowserTest,
                       TwoRegularProfilesGetDistinctServices) {
  ProfileManager* pm = g_browser_process->profile_manager();
  Profile& p2 = profiles::testing::CreateProfileSync(
      pm, pm->user_data_dir().AppendASCII("roamux-updater-p2"));

  RoamuxUpdateService* s1 =
      RoamuxUpdateServiceFactory::GetForProfile(browser()->profile());
  RoamuxUpdateService* s2 = RoamuxUpdateServiceFactory::GetForProfile(&p2);
  EXPECT_NE(nullptr, s1);
  EXPECT_NE(nullptr, s2);
  EXPECT_NE(s1, s2);
}

// Finding 4: the real chrome://settings/help commits (no bad-message renderer
// kill), proving the SettingsUI UpdatePageHandlerFactory binder is registered,
// and its data source exposes updatesAvailable=true for a regular profile.
IN_PROC_BROWSER_TEST_F(RoamuxUpdateServiceFactoryBrowserTest,
                       SettingsHelpCommitsAndUpdatesAvailable) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(),
                                           GURL("chrome://settings/help")));
  content::WebContents* wc =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(wc);
  EXPECT_EQ(GURL("chrome://settings/help"), wc->GetLastCommittedURL());
  // roam-154: chrome://settings is module-based in M149 — loadTimeData is a
  // module export, not a window global — so read it via a dynamic import of the
  // settings bundle (the same singleton the page uses). EvalJs awaits the
  // returned promise.
  EXPECT_EQ(true, content::EvalJs(
                      wc,
                      "import('chrome://settings/settings.js').then("
                      "m => m.loadTimeData.getBoolean('updatesAvailable'))"));
  EXPECT_EQ(true, content::EvalJs(
                      wc,
                      "import('chrome://settings/settings.js').then("
                      "m => m.loadTimeData.getBoolean('roamuxBrandedAbout'))"));
}

}  // namespace
}  // namespace roamux::updates

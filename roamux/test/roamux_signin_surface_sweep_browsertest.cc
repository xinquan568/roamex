// SPDX-License-Identifier: Apache-2.0
// roam-30 (I-5.2) — the §7.3 pref-off surface sweep (plan risk R13): with
// kBraveStyleProfiles on and the opt-in pref at its default (off), every
// enumerated sign-in/sync surface reads suppressed at the concrete gate it
// consults; two profiles stay same-site isolated; flipping the opt-in pref
// re-exposes on restart (PRE_ test); flag-off is upstream-identical.
// TDD/P6: authored and run RED against the unpatched tree before patch 0023.

#include <string>

#include "base/strings/stringprintf.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_test_util.h"
#include "chrome/browser/signin/account_consistency_mode_manager.h"
#include "chrome/browser/signin/signin_promo_util.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/startup/first_run_service.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/webui/signin/signin_ui_error.h"
#include "chrome/browser/ui/webui/signin/signin_utils_desktop.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "components/signin/public/base/signin_buildflags.h"
#include "components/signin/public/base/signin_pref_names.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "google_apis/gaia/gaia_id.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "roamux/common/roamux_features.h"
#include "roamux/common/roamux_prefs.h"
#include "url/gurl.h"

namespace roamux {
namespace {

// Deep-queries open shadow roots for a tag name, polling (Polymer stamps
// asynchronously). Resolves true iff an element with the tag exists within
// the deadline. Same idiom as roamux_tab_strip_position_settings_browsertest.
constexpr char kWaitForTagScript[] = R"(
    (async () => {
      const deepQueryTag = (root, tag) => {
        if (root.querySelector(tag)) return true;
        for (const el of root.querySelectorAll('*')) {
          if (el.shadowRoot && deepQueryTag(el.shadowRoot, tag)) return true;
        }
        return false;
      };
      for (let i = 0; i < %d; i++) {
        if (deepQueryTag(document, '%s')) return true;
        await new Promise(r => setTimeout(r, 100));
      }
      return false;
    })();
)";

std::string WaitForTagScript(const std::string& tag, int attempts) {
  return base::StringPrintf(kWaitForTagScript, attempts, tag.c_str());
}

class RoamuxSigninSurfaceSweepTest : public InProcessBrowserTest {
 public:
  RoamuxSigninSurfaceSweepTest() {
    features_.InitAndEnableFeature(roamux::features::kBraveStyleProfiles);
  }

 private:
  base::test::ScopedFeatureList features_;
};

class RoamuxSigninSurfaceSweepFlagOffTest : public InProcessBrowserTest {
 public:
  RoamuxSigninSurfaceSweepFlagOffTest() {
    features_.InitAndDisableFeature(roamux::features::kBraveStyleProfiles);
  }

 private:
  base::test::ScopedFeatureList features_;
};

// The DICE-dependent contracts (the production hook lives inside ACMM's
// ENABLE_DICE_SUPPORT branch); on Mirror/non-DICE builds these flows cannot
// occur, so the tests compile out with the same guard.
#if BUILDFLAG(ENABLE_DICE_SUPPORT)

// The R13 sweep — one concrete observable per §7.3 inventory item, on a clean
// unmanaged profile with the opt-in pref at its default (off).
IN_PROC_BROWSER_TEST_F(RoamuxSigninSurfaceSweepTest,
                       DefaultSweepAllSurfacesSuppressed) {
  Profile* profile = browser()->profile();
  ASSERT_FALSE(profile->GetPrefs()->GetBoolean(
      roamux::prefs::kSigninOptionalEntryPoint));

  // Shared operative pref — the exact gate profile_menu_view.cc:1026,1058,
  // people_handler.cc:446, first_run_service.cc, and app_menu.cc consult
  // (written by the ACMM startup derivation patch 0023 hooks).
  EXPECT_FALSE(profile->GetPrefs()->GetBoolean(::prefs::kSigninAllowed));

  // Avatar-button / Dice-derived promo machinery: consistency is kDisabled.
  EXPECT_EQ(AccountConsistencyModeManager::GetMethodForProfile(profile),
            signin::AccountConsistencyMethod::kDisabled);

  // Offered-sign-in chokepoint (profile menu / avatar flows).
  EXPECT_FALSE(CanOfferSignin(profile, GaiaId("gaia_id_dummy"),
                              "user@example.com",
                              /*allow_account_from_other_profile=*/true)
                   .IsOk());

  // First-run sign-in (also disabled by the test harness's --no-first-run;
  // the pref gate in first_run_service.cc is the mapping this pins).
  EXPECT_FALSE(ShouldOpenFirstRun(profile));

  // Bookmarks-bar / NTP-family sign-in promos.
  EXPECT_FALSE(signin::ShouldShowBookmarkSignInPromo(*profile));
  EXPECT_FALSE(signin::ShouldShowPasswordSignInPromo(*profile));

  // Settings People/sync surface: the sync account control never stamps.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(),
                                           GURL("chrome://settings/people")));
  content::WebContents* settings =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_EQ(false,
            content::EvalJs(settings,
                            WaitForTagScript("settings-sync-account-control",
                                             /*attempts=*/20)));
}

// Two profiles, same site: cookies never leak across (inherited isolation
// stays intact while suppression is in force).
IN_PROC_BROWSER_TEST_F(RoamuxSigninSurfaceSweepTest,
                       TwoProfilesSameSiteStayIsolated) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL url = embedded_test_server()->GetURL("/empty.html");

  Profile* profile_a = browser()->profile();
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  Profile& profile_b = profiles::testing::CreateProfileSync(
      profile_manager,
      profile_manager->user_data_dir().AppendASCII("RoamuxSweepProfileB"));
  Browser* browser_b = CreateBrowser(&profile_b);

  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser_b, url));

  content::WebContents* tab_a =
      browser()->tab_strip_model()->GetActiveWebContents();
  content::WebContents* tab_b =
      browser_b->tab_strip_model()->GetActiveWebContents();

  ASSERT_TRUE(content::ExecJs(tab_a, "document.cookie = 'roamux_probe=a';"));
  EXPECT_EQ("roamux_probe=a", content::EvalJs(tab_a, "document.cookie"));
  EXPECT_EQ("", content::EvalJs(tab_b, "document.cookie"));

  Profile* raw_b = &profile_b;
  EXPECT_NE(profile_a, raw_b);
  EXPECT_NE(profile_a->GetPath(), raw_b->GetPath());
}

// Opt-in re-expose with restart semantics: PRE_ sets the pref; the relaunched
// run derives kSigninAllowed true at startup.
IN_PROC_BROWSER_TEST_F(RoamuxSigninSurfaceSweepTest,
                       PRE_OptInReexposesOnRestart) {
  PrefService* prefs = browser()->profile()->GetPrefs();
  EXPECT_FALSE(prefs->GetBoolean(::prefs::kSigninAllowed));
  prefs->SetBoolean(roamux::prefs::kSigninOptionalEntryPoint, true);
  prefs->CommitPendingWrite();
}

IN_PROC_BROWSER_TEST_F(RoamuxSigninSurfaceSweepTest, OptInReexposesOnRestart) {
  PrefService* prefs = browser()->profile()->GetPrefs();
  ASSERT_TRUE(prefs->GetBoolean(roamux::prefs::kSigninOptionalEntryPoint));
  EXPECT_TRUE(prefs->GetBoolean(::prefs::kSigninAllowed));
  EXPECT_NE(
      AccountConsistencyModeManager::GetMethodForProfile(browser()->profile()),
      signin::AccountConsistencyMethod::kDisabled);
}

// Flag-off: upstream-identical on a clean unmanaged profile, and the roamux
// opt-in pref has no influence anywhere (promo family pass-through).
IN_PROC_BROWSER_TEST_F(RoamuxSigninSurfaceSweepFlagOffTest,
                       UpstreamBehaviorUntouched) {
  Profile* profile = browser()->profile();
  EXPECT_TRUE(profile->GetPrefs()->GetBoolean(::prefs::kSigninAllowed));
  EXPECT_NE(AccountConsistencyModeManager::GetMethodForProfile(profile),
            signin::AccountConsistencyMethod::kDisabled);
  EXPECT_TRUE(CanOfferSignin(profile, GaiaId("gaia_id_dummy"),
                             "user@example.com",
                             /*allow_account_from_other_profile=*/true)
                  .IsOk());

  // Promo pass-through: toggling the roamux opt-in under the disabled flag
  // must not move any promo gate.
  const bool bookmark_promo_before =
      signin::ShouldShowBookmarkSignInPromo(*profile);
  const bool password_promo_before =
      signin::ShouldShowPasswordSignInPromo(*profile);
  profile->GetPrefs()->SetBoolean(roamux::prefs::kSigninOptionalEntryPoint,
                                  true);
  EXPECT_EQ(signin::ShouldShowBookmarkSignInPromo(*profile),
            bookmark_promo_before);
  EXPECT_EQ(signin::ShouldShowPasswordSignInPromo(*profile),
            password_promo_before);
}

#endif  // BUILDFLAG(ENABLE_DICE_SUPPORT)

}  // namespace
}  // namespace roamux

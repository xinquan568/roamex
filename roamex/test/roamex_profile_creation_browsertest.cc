// SPDX-License-Identifier: Apache-2.0
// roam-29 (I-5.1) route-level contract: with kBraveStyleProfiles on, the
// picker's NEW_PROFILE route computes to localProfileCustomization — the
// sign-in/type-choice step never exists — and the app's automatic
// continueWithoutAccount path creates a local profile with no account/token
// and its own directory/prefs. Flag-off: pure pass-through (type-choice
// preserved where Dice allows). TDD/P6: written RED before patch 0022.

#include <string>

#include "base/json/json_reader.h"
#include "base/run_loop.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/signin/account_consistency_mode_manager.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/profiles/profile_picker.h"
#include "chrome/browser/ui/views/profiles/profile_picker_test_base.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/search_engines_switches.h"
#include "components/signin/public/base/consent_level.h"
#include "components/signin/public/identity_manager/identity_manager.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "roamex/common/roamex_features.h"
#include "url/gurl.h"

namespace roamex {
namespace {

// Clicks the main view's Add-profile button (the user's in-page navigation to
// the NEW_PROFILE route) and atomically reads the route state pushed by the
// picker's navigation mixin plus whether the sign-in/type-choice view is
// active. history.pushState runs synchronously inside click(), so the read
// cannot race the C++ side closing the picker.
constexpr char kClickAddProfileAndReadState[] = R"(
    (() => {
      const app = document.querySelector('profile-picker-app');
      const main = app.shadowRoot.querySelector('profile-picker-main-view');
      const add = main.shadowRoot.querySelector('#addProfile');
      add.click();
      const typeChoice = app.shadowRoot.querySelector('profile-type-choice');
      return JSON.stringify({
        step: history.state.step,
        typeChoiceActive:
            !!(typeChoice && typeChoice.classList.contains('active')),
      });
    })();
)";

class RoamexProfileCreationBrowserTestBase : public ProfilePickerTestBase {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    ProfilePickerTestBase::SetUpCommandLine(command_line);
    // Keep the signed-out post-identity flow deterministic in the harness —
    // the search-engine-choice step must not interpose before FinishFlow.
    command_line->AppendSwitch(switches::kDisableSearchEngineChoiceScreen);
  }

  // Opens the picker on the main view and waits for the WebUI to load.
  void OpenPickerMainView() {
    ProfilePicker::Show(ProfilePicker::Params::FromEntryPoint(
        ProfilePicker::EntryPoint::kProfileMenuManageProfiles));
    WaitForPickerWidgetCreated();
    WaitForLoadStop(GURL("chrome://profile-picker"));
  }

  // Returns {step, typeChoiceActive} observed synchronously after the click.
  std::pair<std::string, bool> ClickAddProfileAndReadState() {
    const std::string json =
        content::EvalJs(web_contents(), kClickAddProfileAndReadState)
            .ExtractString();
    std::optional<base::DictValue> dict =
        base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
    CHECK(dict.has_value());
    return {*dict->FindString("step"),
            dict->FindBool("typeChoiceActive").value_or(true)};
  }
};

class RoamexProfileCreationFlagOnBrowserTest
    : public RoamexProfileCreationBrowserTestBase {
 public:
  RoamexProfileCreationFlagOnBrowserTest() {
    features_.InitAndEnableFeature(roamex::features::kBraveStyleProfiles);
  }

 private:
  base::test::ScopedFeatureList features_;
};

class RoamexProfileCreationFlagOffBrowserTest
    : public RoamexProfileCreationBrowserTestBase {
 public:
  RoamexProfileCreationFlagOffBrowserTest() {
    features_.InitAndDisableFeature(roamex::features::kBraveStyleProfiles);
  }

 private:
  base::test::ScopedFeatureList features_;
};

// Route-level + identity + isolation contract (issue Tests, all three points).
IN_PROC_BROWSER_TEST_F(RoamexProfileCreationFlagOnBrowserTest,
                       CreationIsNameOnlyNoAccountAndIsolated) {
  Profile* original_profile = browser()->profile();
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  const size_t initial_profiles = profile_manager->GetNumberOfProfiles();

  OpenPickerMainView();

  // Entering profile creation resolves straight to the local name-only step
  // (the picker WebUI computes it from the suppressed
  // signInProfileCreationFlowSupported boolean, whose C++ seam is pinned by
  // BraveStyleProfilesTest); the sign-in/type-choice view is never active.
  auto [step, type_choice_active] = ClickAddProfileAndReadState();
  EXPECT_EQ(step, "localProfileCustomization");
  EXPECT_FALSE(type_choice_active);

  // The app's automatic continueWithoutAccount path creates the profile and
  // opens its browser (naming/theme continue in the customization surface).
  Browser* new_browser = ui_test_utils::WaitForBrowserToOpen();
  ASSERT_NE(new_browser, nullptr);
  Profile* new_profile = new_browser->profile();
  ASSERT_NE(new_profile, nullptr);
  EXPECT_EQ(profile_manager->GetNumberOfProfiles(), initial_profiles + 1);

  // No account or token state is created.
  signin::IdentityManager* identity =
      IdentityManagerFactory::GetForProfile(new_profile);
  ASSERT_NE(identity, nullptr);
  EXPECT_TRUE(identity->GetAccountsWithRefreshTokens().empty());
  EXPECT_FALSE(identity->HasPrimaryAccount(signin::ConsentLevel::kSignin));

  // Immediately isolated: own directory and own prefs.
  EXPECT_NE(new_profile, original_profile);
  EXPECT_NE(new_profile->GetPath(), original_profile->GetPath());
  EXPECT_NE(new_profile->GetPrefs(), original_profile->GetPrefs());
}

// Flag-off is a pure pass-through: the boolean equals upstream's Dice value
// and the type-choice step is preserved (where Dice allows).
IN_PROC_BROWSER_TEST_F(RoamexProfileCreationFlagOffBrowserTest,
                       UpstreamTypeChoicePreserved) {
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  const size_t initial_profiles = profile_manager->GetNumberOfProfiles();

  OpenPickerMainView();

  // Pass-through: the route resolves exactly as upstream's Dice value
  // dictates (the boolean reaches the WebUI unmodified when the flag is off).
  const bool dice_allowed =
      AccountConsistencyModeManager::IsDiceSignInAllowed();

  auto [step, type_choice_active] = ClickAddProfileAndReadState();
  if (dice_allowed) {
    EXPECT_EQ(step, "profileTypeChoice");
  } else {
    EXPECT_EQ(step, "localProfileCustomization");
  }

  // No profile is created behind the user's back on the type-choice path.
  if (dice_allowed) {
    base::RunLoop().RunUntilIdle();
    EXPECT_EQ(profile_manager->GetNumberOfProfiles(), initial_profiles);
  }
}

}  // namespace
}  // namespace roamex

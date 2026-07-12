// SPDX-License-Identifier: Apache-2.0
// Hook-level proof (roam-3): Chromium's OWN profile-pref registration path
// registers the roamux.* prefs. RED before
// patches/0004-register-profile-prefs.patch is applied; GREEN after. This is
// the test that fails meaningfully in the hook's absence (the direct registrar
// tests cannot).
//
// Fixture mirrors chrome/browser/prefs/browser_prefs_unittest.cc: keyed-service
// registrars CHECK the UI thread (BrowserTaskEnvironment) and need the
// factories built; the Chrome unit-test main
// (//chrome/test:test_support_unit) supplies the TestingBrowserProcess.

#include "chrome/browser/prefs/browser_prefs.h"
#include "chrome/browser/profiles/chrome_browser_main_extra_parts_profiles.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "content/public/test/browser_task_environment.h"
#include "roamux/common/roamux_prefs.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

class RoamuxBrowserPrefsHookTest : public testing::Test {
protected:
  RoamuxBrowserPrefsHookTest() {
    ChromeBrowserMainExtraPartsProfiles::
        EnsureBrowserContextKeyedServiceFactoriesBuilt();
    RegisterUserProfilePrefs(prefs_.registry());
  }

  content::BrowserTaskEnvironment task_environment_;
  sync_preferences::TestingPrefServiceSyncable prefs_;
};

TEST_F(RoamuxBrowserPrefsHookTest, UpstreamRegistrationIncludesRoamuxPrefs) {
  EXPECT_NE(prefs_.FindPreference(roamux::prefs::kTabStripPosition), nullptr)
      << "upstream RegisterUserProfilePrefs did not register "
      << roamux::prefs::kTabStripPosition
      << " - is patches/0004-register-profile-prefs.patch applied?";
  EXPECT_NE(prefs_.FindPreference(roamux::prefs::kReopenClosed), nullptr);
  EXPECT_NE(prefs_.FindPreference(roamux::prefs::kSigninOptionalEntryPoint),
            nullptr);
}

} // namespace

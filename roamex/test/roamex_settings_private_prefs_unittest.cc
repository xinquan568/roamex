// SPDX-License-Identifier: Apache-2.0
// roam-6 (I-1.1) WB-T3: the settings transport layer —
// roamex.tabs.strip_position must be exposed through the settings_private
// allowlist (patch 0006) so the Appearance dropdown's pref binding can read and
// write it live (TDD/P6: written RED before the patch).

#include <optional>

#include "base/values.h"
#include "chrome/browser/extensions/api/settings_private/prefs_util.h"
#include "chrome/common/extensions/api/settings_private.h"
#include "chrome/test/base/testing_profile.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_task_environment.h"
#include "roamex/common/roamex_prefs.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace settings_api = extensions::api::settings_private;

namespace roamex {
namespace {

class RoamexSettingsPrivatePrefsTest : public testing::Test {
 protected:
  content::BrowserTaskEnvironment task_environment_;
  TestingProfile profile_;
  extensions::PrefsUtil prefs_util_{&profile_};
};

// roam-31: the sign-in opt-in pref is allowlisted as BOOLEAN and round-trips.
TEST_F(RoamexSettingsPrivatePrefsTest, SigninOptInIsAllowlistedAsBoolean) {
  std::optional<settings_api::PrefObject> pref =
      prefs_util_.GetPref(roamex::prefs::kSigninOptionalEntryPoint);
  ASSERT_TRUE(pref.has_value());
  EXPECT_EQ(settings_api::PrefType::kBoolean, pref->type);
  EXPECT_EQ(false, pref->value->GetBool());
}

TEST_F(RoamexSettingsPrivatePrefsTest, SigninOptInRoundTripsThroughSetPref) {
  base::Value on(true);
  EXPECT_EQ(extensions::settings_private::SetPrefResult::SUCCESS,
            prefs_util_.SetPref(roamex::prefs::kSigninOptionalEntryPoint, &on));
  EXPECT_TRUE(profile_.GetPrefs()->GetBoolean(
      roamex::prefs::kSigninOptionalEntryPoint));
  std::optional<settings_api::PrefObject> pref =
      prefs_util_.GetPref(roamex::prefs::kSigninOptionalEntryPoint);
  ASSERT_TRUE(pref.has_value());
  EXPECT_EQ(true, pref->value->GetBool());
}

TEST_F(RoamexSettingsPrivatePrefsTest, TabStripPositionIsAllowlistedAsNumber) {
  const auto& keys = prefs_util_.GetAllowlistedKeys();
  auto it = keys.find(prefs::kTabStripPosition);
  ASSERT_NE(keys.end(), it) << prefs::kTabStripPosition
                            << " missing from the settings_private allowlist";
  EXPECT_EQ(settings_api::PrefType::kNumber, it->second);
}

TEST_F(RoamexSettingsPrivatePrefsTest, GetPrefExposesTheRegisteredDefault) {
  std::optional<settings_api::PrefObject> pref =
      prefs_util_.GetPref(prefs::kTabStripPosition);
  ASSERT_TRUE(pref.has_value());
  EXPECT_EQ(settings_api::PrefType::kNumber, pref->type);
  ASSERT_TRUE(pref->value.has_value());
  EXPECT_EQ(0, pref->value->GetIfInt().value_or(-1));
}

TEST_F(RoamexSettingsPrivatePrefsTest, SetPrefRoundTripsAndReadsLive) {
  base::Value two(2);
  EXPECT_EQ(extensions::settings_private::SetPrefResult::SUCCESS,
            prefs_util_.SetPref(prefs::kTabStripPosition, &two));
  EXPECT_EQ(2, profile_.GetPrefs()->GetInteger(prefs::kTabStripPosition));

  // A direct PrefService write must be visible through the settings_private
  // read path (the live half of "the UI reflects it").
  profile_.GetPrefs()->SetInteger(prefs::kTabStripPosition, 3);
  std::optional<settings_api::PrefObject> pref =
      prefs_util_.GetPref(prefs::kTabStripPosition);
  ASSERT_TRUE(pref.has_value());
  ASSERT_TRUE(pref->value.has_value());
  EXPECT_EQ(3, pref->value->GetIfInt().value_or(-1));
}

}  // namespace
}  // namespace roamex

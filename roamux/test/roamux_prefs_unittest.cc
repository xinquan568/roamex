// SPDX-License-Identifier: Apache-2.0
// Direct registrar proofs (roam-3): each roamux.* pref registers with its
// documented default and round-trips through a real PrefService. These guard
// registrar semantics; the hook-level proof (upstream registration path) lives
// in roamux_browser_prefs_hook_unittest.cc.

#include "components/prefs/testing_pref_service.h"
#include "roamux/common/roamux_prefs.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

class RoamuxPrefsTest : public testing::Test {
protected:
  RoamuxPrefsTest() { roamux::prefs::RegisterProfilePrefs(prefs_.registry()); }

  TestingPrefServiceSimple prefs_;
};

TEST_F(RoamuxPrefsTest, AllThreePrefsRegisterWithDefaults) {
  ASSERT_NE(prefs_.FindPreference(roamux::prefs::kTabStripPosition), nullptr);
  ASSERT_NE(prefs_.FindPreference(roamux::prefs::kReopenClosed), nullptr);
  ASSERT_NE(prefs_.FindPreference(roamux::prefs::kSigninOptionalEntryPoint),
            nullptr);
  EXPECT_EQ(prefs_.GetInteger(roamux::prefs::kTabStripPosition), 0); // top
  EXPECT_FALSE(prefs_.GetBoolean(roamux::prefs::kReopenClosed));
  EXPECT_FALSE(prefs_.GetBoolean(roamux::prefs::kSigninOptionalEntryPoint));
}

TEST_F(RoamuxPrefsTest, TabStripPositionRoundTrips) {
  prefs_.SetInteger(roamux::prefs::kTabStripPosition, 3); // right
  EXPECT_EQ(prefs_.GetInteger(roamux::prefs::kTabStripPosition), 3);
}

TEST_F(RoamuxPrefsTest, ReopenClosedRoundTrips) {
  prefs_.SetBoolean(roamux::prefs::kReopenClosed, true);
  EXPECT_TRUE(prefs_.GetBoolean(roamux::prefs::kReopenClosed));
}

TEST_F(RoamuxPrefsTest, SigninOptionalEntryPointRoundTrips) {
  prefs_.SetBoolean(roamux::prefs::kSigninOptionalEntryPoint, true);
  EXPECT_TRUE(prefs_.GetBoolean(roamux::prefs::kSigninOptionalEntryPoint));
}

} // namespace

// SPDX-License-Identifier: Apache-2.0
#include "base/feature_list.h"
#include "roamux/common/roamux_features.h"
#include "roamux/common/roamux_prefs.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

// E0 smoke (roam-1 / roam-3): the //roamux overlay links; feature flags ship
// disabled by default until their epic completes and graduates the flag.
// Graduated to default-ON (user-toggleable via chrome://flags): roam-185 (E1,
// kTabStripPosition), roam-187 (E2, kInitialUrl), roam-189 (E4, kTabVisitNav),
// roam-190 (E3, kEdgeImport). The still-in-progress epics keep their disabled
// default.
TEST(RoamuxSmokeTest, FeatureFlagsDefaultDisabled) {
  EXPECT_TRUE(
      base::FeatureList::IsEnabled(roamux::features::kTabStripPosition));
  EXPECT_TRUE(base::FeatureList::IsEnabled(roamux::features::kInitialUrl));
  EXPECT_TRUE(base::FeatureList::IsEnabled(roamux::features::kTabVisitNav));
  EXPECT_TRUE(base::FeatureList::IsEnabled(roamux::features::kEdgeImport));
  EXPECT_FALSE(
      base::FeatureList::IsEnabled(roamux::features::kBraveStyleProfiles));
}

// roam-179 (E8): the scheme alias/display flag SHIPS ENABLED — the rebrand
// epic's user-visible branding is on by default, with the flag kept as a
// kill-switch (D3; flag-off identity is proven at the browser level).
TEST(RoamuxSmokeTest, SchemeAliasShipsEnabled) {
  EXPECT_TRUE(
      base::FeatureList::IsEnabled(roamux::features::kRoamuxSchemeAlias));
}

// Pref keys are stable, local, and namespaced under "roamux." (plan §7.2).
TEST(RoamuxSmokeTest, PrefKeysAreNamespaced) {
  EXPECT_STREQ("roamux.tabs.strip_position", roamux::prefs::kTabStripPosition);
  EXPECT_STREQ("roamux.signin.optional_entry_point_enabled",
               roamux::prefs::kSigninOptionalEntryPoint);
}

}  // namespace

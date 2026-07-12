// SPDX-License-Identifier: Apache-2.0
#include "base/feature_list.h"
#include "roamux/common/roamux_features.h"
#include "roamux/common/roamux_prefs.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

// E0 smoke (roam-1 / roam-3): the //roamux overlay links, and every feature flag ships DISABLED by
// default (plan P3 — nothing behavioral is on until its epic completes).
TEST(RoamuxSmokeTest, FeatureFlagsDefaultDisabled) {
  EXPECT_FALSE(base::FeatureList::IsEnabled(roamux::features::kTabStripPosition));
  EXPECT_FALSE(base::FeatureList::IsEnabled(roamux::features::kInitialUrl));
  EXPECT_FALSE(base::FeatureList::IsEnabled(roamux::features::kEdgeImport));
  EXPECT_FALSE(base::FeatureList::IsEnabled(roamux::features::kTabVisitNav));
  EXPECT_FALSE(base::FeatureList::IsEnabled(roamux::features::kBraveStyleProfiles));
}

// Pref keys are stable, local, and namespaced under "roamux." (plan §7.2).
TEST(RoamuxSmokeTest, PrefKeysAreNamespaced) {
  EXPECT_STREQ("roamux.tabs.strip_position", roamux::prefs::kTabStripPosition);
  EXPECT_STREQ("roamux.signin.optional_entry_point_enabled",
               roamux::prefs::kSigninOptionalEntryPoint);
}

}  // namespace

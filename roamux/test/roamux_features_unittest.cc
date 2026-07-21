// SPDX-License-Identifier: Apache-2.0
// Feature-flag scaffolding proof (roam-3): a Roamux base::Feature toggles on
// demand while the others keep their disabled default (the roam-1 smoke test
// guards all five defaults).

#include "roamux/common/roamux_features.h"

#include "base/feature_list.h"
#include "base/test/scoped_feature_list.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

TEST(RoamuxFeaturesTest, FeatureTogglesOnUnderScopedFeatureList) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndEnableFeature(roamux::features::kTabVisitNav);
  EXPECT_TRUE(base::FeatureList::IsEnabled(roamux::features::kTabVisitNav));
  // A flag not named in the override keeps its (disabled) default. roam-185
  // and roam-187 shipped their flags default-ON; kBraveStyleProfiles (E5, not
  // scheduled to graduate) is the stable still-disabled example.
  EXPECT_FALSE(
      base::FeatureList::IsEnabled(roamux::features::kBraveStyleProfiles));
}

TEST(RoamuxFeaturesTest, TabStripPositionEnabledByDefault) {
  // roam-185: the E1 tab-strip-position feature ships enabled by default (the
  // chrome://flags entry lets users opt out).
  EXPECT_TRUE(
      base::FeatureList::IsEnabled(roamux::features::kTabStripPosition));
}

TEST(RoamuxFeaturesTest, InitialUrlEnabledByDefault) {
  // roam-187: the E2 per-tab initial-URL feature ships enabled by default
  // (chrome://flags/#roamux-initial-url lets users opt out).
  EXPECT_TRUE(base::FeatureList::IsEnabled(roamux::features::kInitialUrl));
}

TEST(RoamuxFeaturesTest, TabVisitNavEnabledByDefault) {
  // roam-189: the E4 tab visit-order navigation ships enabled by default
  // (chrome://flags/#roamux-tab-visit-nav lets users opt out).
  EXPECT_TRUE(base::FeatureList::IsEnabled(roamux::features::kTabVisitNav));
}

TEST(RoamuxFeaturesTest, EdgeImportEnabledByDefault) {
  // roam-190: the E3 Microsoft Edge import ships enabled by default
  // (chrome://flags/#roamux-edge-import lets users opt out).
  EXPECT_TRUE(base::FeatureList::IsEnabled(roamux::features::kEdgeImport));
}

}  // namespace

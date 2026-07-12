// SPDX-License-Identifier: Apache-2.0
// Name-only profile creation decision table (roam-29, plan §14/E5): the
// sign-in creation step is offered iff upstream allows it AND
// kBraveStyleProfiles is off. Flag-off must be a pure pass-through.

#include "roamux/browser/profiles/brave_style_profiles.h"

#include "base/test/scoped_feature_list.h"
#include "roamux/common/roamux_features.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

using roamux::profiles::AllowSigninProfileCreationStep;
using roamux::profiles::IsNameOnlyProfileCreationEnabled;

TEST(BraveStyleProfilesTest, FlagOffPassesUpstreamValueThrough) {
  base::test::ScopedFeatureList features;
  features.InitAndDisableFeature(roamux::features::kBraveStyleProfiles);
  EXPECT_FALSE(IsNameOnlyProfileCreationEnabled());
  EXPECT_TRUE(AllowSigninProfileCreationStep(/*upstream_allowed=*/true));
  EXPECT_FALSE(AllowSigninProfileCreationStep(/*upstream_allowed=*/false));
}

TEST(BraveStyleProfilesTest, FlagOnSuppressesSigninStepRegardlessOfUpstream) {
  base::test::ScopedFeatureList features;
  features.InitAndEnableFeature(roamux::features::kBraveStyleProfiles);
  EXPECT_TRUE(IsNameOnlyProfileCreationEnabled());
  EXPECT_FALSE(AllowSigninProfileCreationStep(/*upstream_allowed=*/true));
  EXPECT_FALSE(AllowSigninProfileCreationStep(/*upstream_allowed=*/false));
}

}  // namespace

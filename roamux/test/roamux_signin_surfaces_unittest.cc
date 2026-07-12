// SPDX-License-Identifier: Apache-2.0
// roam-30 (I-5.2): the sign-in-allowed-on-next-startup decision matrix — the
// ACMM derivation input consumed by patch 0023. Flag off ⇒ pure passthrough
// of the upstream pref (the roamux opt-in is ignored); flag on ⇒ opt-in AND
// upstream (policy/user false always wins). TDD/P6: written RED first.

#include "base/test/scoped_command_line.h"
#include "base/test/scoped_feature_list.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "components/signin/public/base/signin_pref_names.h"
#include "roamux/browser/signin/signin_surfaces.h"
#include "roamux/common/roamux_features.h"
#include "roamux/common/roamux_prefs.h"
#include "roamux/common/roamux_switches.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

using roamux::signin::IsSigninAllowedOnNextStartup;

class RoamuxSigninSurfacesTest : public testing::Test {
 protected:
  RoamuxSigninSurfacesTest() {
    prefs_.registry()->RegisterBooleanPref(prefs::kSigninAllowedOnNextStartup,
                                           true);
    prefs_.registry()->RegisterBooleanPref(
        roamux::prefs::kSigninOptionalEntryPoint, false);
  }

  void SetInputs(bool opt_in, bool upstream) {
    prefs_.SetBoolean(roamux::prefs::kSigninOptionalEntryPoint, opt_in);
    prefs_.SetBoolean(prefs::kSigninAllowedOnNextStartup, upstream);
  }

  TestingPrefServiceSimple prefs_;
};

// Rows 1–4: flag off ⇒ passthrough of upstream; the roamux opt-in is ignored.
TEST_F(RoamuxSigninSurfacesTest, FlagOffPassesUpstreamThroughIgnoringOptIn) {
  base::test::ScopedFeatureList features;
  features.InitAndDisableFeature(roamux::features::kBraveStyleProfiles);

  SetInputs(/*opt_in=*/false, /*upstream=*/true);
  EXPECT_TRUE(IsSigninAllowedOnNextStartup(&prefs_));
  SetInputs(/*opt_in=*/false, /*upstream=*/false);
  EXPECT_FALSE(IsSigninAllowedOnNextStartup(&prefs_));
  SetInputs(/*opt_in=*/true, /*upstream=*/true);
  EXPECT_TRUE(IsSigninAllowedOnNextStartup(&prefs_));
  SetInputs(/*opt_in=*/true, /*upstream=*/false);
  EXPECT_FALSE(IsSigninAllowedOnNextStartup(&prefs_));
}

// Rows 5–6: flag on + opt-in off ⇒ suppressed even when upstream allows.
TEST_F(RoamuxSigninSurfacesTest, FlagOnOptInOffSuppressesRegardlessOfUpstream) {
  base::test::ScopedFeatureList features;
  features.InitAndEnableFeature(roamux::features::kBraveStyleProfiles);

  SetInputs(/*opt_in=*/false, /*upstream=*/true);
  EXPECT_FALSE(IsSigninAllowedOnNextStartup(&prefs_));
  SetInputs(/*opt_in=*/false, /*upstream=*/false);
  EXPECT_FALSE(IsSigninAllowedOnNextStartup(&prefs_));
}

// Rows 7–8: flag on + opt-in on ⇒ defer to upstream (policy false still wins).
TEST_F(RoamuxSigninSurfacesTest, FlagOnOptInOnDefersToUpstream) {
  base::test::ScopedFeatureList features;
  features.InitAndEnableFeature(roamux::features::kBraveStyleProfiles);

  SetInputs(/*opt_in=*/true, /*upstream=*/true);
  EXPECT_TRUE(IsSigninAllowedOnNextStartup(&prefs_));
  SetInputs(/*opt_in=*/true, /*upstream=*/false);
  EXPECT_FALSE(IsSigninAllowedOnNextStartup(&prefs_));
}

// roam-31: the flags-switch mirror. Flag on + pref off + switch ⇒ opted in
// (defers to upstream); flag off ⇒ the switch is ignored (pure passthrough).
TEST_F(RoamuxSigninSurfacesTest, FlagOnSwitchActsAsOptIn) {
  base::test::ScopedFeatureList features;
  features.InitAndEnableFeature(roamux::features::kBraveStyleProfiles);
  base::test::ScopedCommandLine command_line;
  command_line.GetProcessCommandLine()->AppendSwitch(
      roamux::switches::kSigninOptIn);

  SetInputs(/*opt_in=*/false, /*upstream=*/true);
  EXPECT_TRUE(IsSigninAllowedOnNextStartup(&prefs_));
  SetInputs(/*opt_in=*/false, /*upstream=*/false);
  EXPECT_FALSE(IsSigninAllowedOnNextStartup(&prefs_));
}

TEST_F(RoamuxSigninSurfacesTest, FlagOffIgnoresSwitch) {
  base::test::ScopedFeatureList features;
  features.InitAndDisableFeature(roamux::features::kBraveStyleProfiles);
  base::test::ScopedCommandLine command_line;
  command_line.GetProcessCommandLine()->AppendSwitch(
      roamux::switches::kSigninOptIn);

  SetInputs(/*opt_in=*/false, /*upstream=*/true);
  EXPECT_TRUE(IsSigninAllowedOnNextStartup(&prefs_));
  SetInputs(/*opt_in=*/false, /*upstream=*/false);
  EXPECT_FALSE(IsSigninAllowedOnNextStartup(&prefs_));
}

}  // namespace

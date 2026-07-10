// SPDX-License-Identifier: Apache-2.0
// roam-30 (I-5.2): the sign-in-allowed-on-next-startup decision matrix — the
// ACMM derivation input consumed by patch 0023. Flag off ⇒ pure passthrough
// of the upstream pref (the roamex opt-in is ignored); flag on ⇒ opt-in AND
// upstream (policy/user false always wins). TDD/P6: written RED first.

#include "base/test/scoped_feature_list.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "components/signin/public/base/signin_pref_names.h"
#include "roamex/browser/signin/signin_surfaces.h"
#include "roamex/common/roamex_features.h"
#include "roamex/common/roamex_prefs.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

using roamex::signin::IsSigninAllowedOnNextStartup;

class RoamexSigninSurfacesTest : public testing::Test {
 protected:
  RoamexSigninSurfacesTest() {
    prefs_.registry()->RegisterBooleanPref(prefs::kSigninAllowedOnNextStartup,
                                           true);
    prefs_.registry()->RegisterBooleanPref(
        roamex::prefs::kSigninOptionalEntryPoint, false);
  }

  void SetInputs(bool opt_in, bool upstream) {
    prefs_.SetBoolean(roamex::prefs::kSigninOptionalEntryPoint, opt_in);
    prefs_.SetBoolean(prefs::kSigninAllowedOnNextStartup, upstream);
  }

  TestingPrefServiceSimple prefs_;
};

// Rows 1–4: flag off ⇒ passthrough of upstream; the roamex opt-in is ignored.
TEST_F(RoamexSigninSurfacesTest, FlagOffPassesUpstreamThroughIgnoringOptIn) {
  base::test::ScopedFeatureList features;
  features.InitAndDisableFeature(roamex::features::kBraveStyleProfiles);

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
TEST_F(RoamexSigninSurfacesTest, FlagOnOptInOffSuppressesRegardlessOfUpstream) {
  base::test::ScopedFeatureList features;
  features.InitAndEnableFeature(roamex::features::kBraveStyleProfiles);

  SetInputs(/*opt_in=*/false, /*upstream=*/true);
  EXPECT_FALSE(IsSigninAllowedOnNextStartup(&prefs_));
  SetInputs(/*opt_in=*/false, /*upstream=*/false);
  EXPECT_FALSE(IsSigninAllowedOnNextStartup(&prefs_));
}

// Rows 7–8: flag on + opt-in on ⇒ defer to upstream (policy false still wins).
TEST_F(RoamexSigninSurfacesTest, FlagOnOptInOnDefersToUpstream) {
  base::test::ScopedFeatureList features;
  features.InitAndEnableFeature(roamex::features::kBraveStyleProfiles);

  SetInputs(/*opt_in=*/true, /*upstream=*/true);
  EXPECT_TRUE(IsSigninAllowedOnNextStartup(&prefs_));
  SetInputs(/*opt_in=*/true, /*upstream=*/false);
  EXPECT_FALSE(IsSigninAllowedOnNextStartup(&prefs_));
}

}  // namespace

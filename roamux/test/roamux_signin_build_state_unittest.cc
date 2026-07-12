// SPDX-License-Identifier: Apache-2.0
// Build-state proofs (roam-4, plan SS12.3): the three-state decision table via
// injected inputs, the reference-build BUILDFLAG wiring assert, and the
// end-to-end keyless/inert asserts (env-guarded).

#include <cstdlib>

#include "roamux/browser/signin/roamux_signin_build_state.h"
#include "roamux/browser/signin/signin_buildflags.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

using roamux::signin::BuildState;
using roamux::signin::GetBuildState;
using roamux::signin::GetBuildStateForInputs;
using roamux::signin::IsSigninInert;

// Decision table (SS7.3/SS12.3): keys are necessary but not sufficient.
TEST(RoamuxSigninBuildStateTest, DecisionTableCoversAllInputCombinations) {
  EXPECT_EQ(GetBuildStateForInputs(false, false), BuildState::kKeyless);
  EXPECT_EQ(GetBuildStateForInputs(false, true), BuildState::kKeyless);
  EXPECT_EQ(GetBuildStateForInputs(true, false),
            BuildState::kKeyedUnauthorized);
  EXPECT_EQ(GetBuildStateForInputs(true, true), BuildState::kKeyedAuthorized);
}

// S5-1: the reference build's authorization flag must be compiled in as FALSE.
// A hard-coded or miswired GN flag fails here even while the keyless state
// would mask it in GetBuildState().
TEST(RoamuxSigninBuildStateTest, ReferenceBuildAuthorizationFlagIsFalse) {
  constexpr bool kAuthorized = BUILDFLAG(ROAMUX_SIGNIN_AUTHORIZED_BUILD);
  EXPECT_FALSE(kAuthorized)
      << "the reference build must never assert Google authorization "
         "(roamux_signin_authorized_build is a distributor-only GN arg)";
}

// End-to-end (issue Tests 1+3): the reference build is keyless and sign-in is
// inert. S5-2: skip loudly (never a false green/red) if Google key env vars
// could alter what the binary observes.
TEST(RoamuxSigninBuildStateTest, ReferenceBuildIsKeylessAndInert) {
  for (const char *var : {"GOOGLE_API_KEY", "GOOGLE_DEFAULT_CLIENT_ID",
                          "GOOGLE_DEFAULT_CLIENT_SECRET"}) {
    if (std::getenv(var) != nullptr) {
      GTEST_SKIP() << var
                   << " is set in this environment; the end-to-end keyless "
                      "assert is only meaningful on a key-free machine.";
    }
  }
  EXPECT_EQ(GetBuildState(), BuildState::kKeyless);
  EXPECT_TRUE(IsSigninInert());
}

} // namespace

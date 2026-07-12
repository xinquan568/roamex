// SPDX-License-Identifier: Apache-2.0
// roam-31 (I-5.3): the inert-with-explanation decision table over injected
// build states, and the state-specific explanation texts. The
// keyed-and-authorized clause is covered at this injected seam (a real
// keyed+authorized build is a distributor acceptance item, not a CI state).
// TDD/P6: written RED first.

#include "roamux/browser/signin/roamux_signin_build_state.h"
#include "roamux/browser/signin/signin_inert.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

using roamux::signin::BuildState;
using roamux::signin::GetInertSigninExplanation;
using roamux::signin::ShouldInterceptSigninForState;

TEST(RoamuxSigninInertTest, FlagOffNeverIntercepts) {
  EXPECT_FALSE(ShouldInterceptSigninForState(false, BuildState::kKeyless));
  EXPECT_FALSE(
      ShouldInterceptSigninForState(false, BuildState::kKeyedUnauthorized));
  EXPECT_FALSE(
      ShouldInterceptSigninForState(false, BuildState::kKeyedAuthorized));
}

TEST(RoamuxSigninInertTest, FlagOnInterceptsInertStatesOnly) {
  EXPECT_TRUE(ShouldInterceptSigninForState(true, BuildState::kKeyless));
  EXPECT_TRUE(
      ShouldInterceptSigninForState(true, BuildState::kKeyedUnauthorized));
  // Keyed-and-authorized: the standard Chromium flow runs (pass-through).
  EXPECT_FALSE(
      ShouldInterceptSigninForState(true, BuildState::kKeyedAuthorized));
}

TEST(RoamuxSigninInertTest, ExplanationsAreStateSpecificAndHonest) {
  const std::u16string keyless =
      GetInertSigninExplanation(BuildState::kKeyless);
  const std::u16string unauthorized =
      GetInertSigninExplanation(BuildState::kKeyedUnauthorized);
  EXPECT_FALSE(keyless.empty());
  EXPECT_FALSE(unauthorized.empty());
  EXPECT_NE(keyless, unauthorized);
  EXPECT_TRUE(GetInertSigninExplanation(BuildState::kKeyedAuthorized).empty());
}

}  // namespace

// SPDX-License-Identifier: Apache-2.0
#include "roamux/browser/scheme/roamux_scheme_rewrite.h"

#include "base/test/scoped_feature_list.h"
#include "roamux/common/roamux_features.h"
#include "roamux/common/roamux_url_constants.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/url_util.h"

namespace roamux {
namespace {

// roam-91: the curated roamux:// → chrome:// alias map (TDD red-first).
//
// In production both "roamux" (patch 0028) and "chrome" (content) are
// registered standard schemes before any rewrite runs. A bare unit-test binary
// registers neither, and an unregistered scheme parses HOSTLESS — so the
// fixture registers both inside a scoped registry or every host comparison
// here would be vacuous.
class RoamuxSchemeRewriteTest : public testing::Test {
protected:
  RoamuxSchemeRewriteTest() {
    url::AddStandardScheme("roamux", url::SCHEME_WITH_HOST);
    // The pre-rebrand "roamex" scheme (killed in roam-93) — kept as a literal
    // so the dieback test below can prove the alias map never speaks it. This
    // historical string is deliberately NOT renamed to roamux (roam-94).
    url::AddStandardScheme("roamex", url::SCHEME_WITH_HOST);
    url::AddStandardScheme("chrome", url::SCHEME_WITH_HOST);
  }

  url::ScopedSchemeRegistryForTests scheme_registry_;
  base::test::ScopedFeatureList features_;
};

// Pins the patch-0028 literal ↔ overlay-constant agreement and the registered
// parse shape (host must not be empty once "roamux" is standard).
TEST_F(RoamuxSchemeRewriteTest, SchemeConstantPinsRegisteredParse) {
  GURL url("roamux://about");
  EXPECT_EQ(kRoamuxScheme, url.GetScheme());
  EXPECT_EQ(kRoamuxAliasAboutHost, url.GetHost());
}

TEST_F(RoamuxSchemeRewriteTest, RewritesAboutHostWhenEnabled) {
  features_.InitAndEnableFeature(features::kRoamuxSchemeAlias);
  GURL url("roamux://about");
  // Chaining idiom: the handler mutates in place but ALWAYS returns false.
  EXPECT_FALSE(MaybeRewriteRoamuxAliasURL(&url, nullptr));
  EXPECT_EQ("chrome://roamux-about/", url.spec());
}

TEST_F(RoamuxSchemeRewriteTest, RewritesFlagsPreservingPathAndRef) {
  features_.InitAndEnableFeature(features::kRoamuxSchemeAlias);
  GURL url("roamux://flags/#roamux-scheme-alias");
  EXPECT_FALSE(MaybeRewriteRoamuxAliasURL(&url, nullptr));
  EXPECT_EQ("chrome://flags/#roamux-scheme-alias", url.spec());
}

TEST_F(RoamuxSchemeRewriteTest, LeavesUnmappedRoamuxHostUntouched) {
  features_.InitAndEnableFeature(features::kRoamuxSchemeAlias);
  GURL url("roamux://settings");
  const GURL original = url;
  EXPECT_FALSE(MaybeRewriteRoamuxAliasURL(&url, nullptr));
  EXPECT_EQ(original, url);
}

// The issue's no-shadowing negative: real chrome:// URLs — including the alias
// TARGET — are never touched.
TEST_F(RoamuxSchemeRewriteTest, NeverTouchesChromeUrls) {
  features_.InitAndEnableFeature(features::kRoamuxSchemeAlias);
  for (const char *spec : {"chrome://roamux-about/", "chrome://settings/",
                           "chrome://about/", "chrome://flags/"}) {
    GURL url(spec);
    const GURL original = url;
    EXPECT_FALSE(MaybeRewriteRoamuxAliasURL(&url, nullptr));
    EXPECT_EQ(original, url) << spec;
  }
}

TEST_F(RoamuxSchemeRewriteTest, NeverTouchesWebSchemes) {
  features_.InitAndEnableFeature(features::kRoamuxSchemeAlias);
  GURL url("https://example.com/about");
  const GURL original = url;
  EXPECT_FALSE(MaybeRewriteRoamuxAliasURL(&url, nullptr));
  EXPECT_EQ(original, url);
}

// P3: the feature ships disabled — flag-off must be a strict no-op even for
// mapped hosts.
TEST_F(RoamuxSchemeRewriteTest, FlagOffIsInert) {
  features_.InitAndDisableFeature(features::kRoamuxSchemeAlias);
  GURL url("roamux://about");
  const GURL original = url;
  EXPECT_FALSE(MaybeRewriteRoamuxAliasURL(&url, nullptr));
  EXPECT_EQ(original, url);
}

// roam-93 dieback (D2a): the OLD roamex:// scheme (pre-rebrand) is no longer
// aliased even with the feature enabled — the curated map speaks roamux://
// only.
TEST_F(RoamuxSchemeRewriteTest, OldRoamexSchemeIsNoLongerAliased) {
  features_.InitAndEnableFeature(features::kRoamuxSchemeAlias);
  GURL url("roamex://about");
  const GURL original = url;
  EXPECT_FALSE(MaybeRewriteRoamuxAliasURL(&url, nullptr));
  EXPECT_EQ(original, url);
}

} // namespace
} // namespace roamux

// SPDX-License-Identifier: Apache-2.0
#include "roamex/browser/scheme/roamex_scheme_rewrite.h"

#include "base/test/scoped_feature_list.h"
#include "roamex/common/roamex_features.h"
#include "roamex/common/roamex_url_constants.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/url_util.h"

namespace roamex {
namespace {

// roam-91: the curated roamex:// → chrome:// alias map (TDD red-first).
//
// In production both "roamex" (patch 0028) and "chrome" (content) are
// registered standard schemes before any rewrite runs. A bare unit-test binary
// registers neither, and an unregistered scheme parses HOSTLESS — so the
// fixture registers both inside a scoped registry or every host comparison
// here would be vacuous.
class RoamexSchemeRewriteTest : public testing::Test {
protected:
  RoamexSchemeRewriteTest() {
    url::AddStandardScheme("roamux", url::SCHEME_WITH_HOST);
    url::AddStandardScheme("roamex", url::SCHEME_WITH_HOST);
    url::AddStandardScheme("chrome", url::SCHEME_WITH_HOST);
  }

  url::ScopedSchemeRegistryForTests scheme_registry_;
  base::test::ScopedFeatureList features_;
};

// Pins the patch-0028 literal ↔ overlay-constant agreement and the registered
// parse shape (host must not be empty once "roamex" is standard).
TEST_F(RoamexSchemeRewriteTest, SchemeConstantPinsRegisteredParse) {
  GURL url("roamux://about");
  EXPECT_EQ(kRoamexScheme, url.GetScheme());
  EXPECT_EQ(kRoamexAliasAboutHost, url.GetHost());
}

TEST_F(RoamexSchemeRewriteTest, RewritesAboutHostWhenEnabled) {
  features_.InitAndEnableFeature(features::kRoamexSchemeAlias);
  GURL url("roamux://about");
  // Chaining idiom: the handler mutates in place but ALWAYS returns false.
  EXPECT_FALSE(MaybeRewriteRoamexAliasURL(&url, nullptr));
  EXPECT_EQ("chrome://roamux-about/", url.spec());
}

TEST_F(RoamexSchemeRewriteTest, RewritesFlagsPreservingPathAndRef) {
  features_.InitAndEnableFeature(features::kRoamexSchemeAlias);
  GURL url("roamux://flags/#roamux-scheme-alias");
  EXPECT_FALSE(MaybeRewriteRoamexAliasURL(&url, nullptr));
  EXPECT_EQ("chrome://flags/#roamux-scheme-alias", url.spec());
}

TEST_F(RoamexSchemeRewriteTest, LeavesUnmappedRoamexHostUntouched) {
  features_.InitAndEnableFeature(features::kRoamexSchemeAlias);
  GURL url("roamux://settings");
  const GURL original = url;
  EXPECT_FALSE(MaybeRewriteRoamexAliasURL(&url, nullptr));
  EXPECT_EQ(original, url);
}

// The issue's no-shadowing negative: real chrome:// URLs — including the alias
// TARGET — are never touched.
TEST_F(RoamexSchemeRewriteTest, NeverTouchesChromeUrls) {
  features_.InitAndEnableFeature(features::kRoamexSchemeAlias);
  for (const char *spec : {"chrome://roamux-about/", "chrome://settings/",
                           "chrome://about/", "chrome://flags/"}) {
    GURL url(spec);
    const GURL original = url;
    EXPECT_FALSE(MaybeRewriteRoamexAliasURL(&url, nullptr));
    EXPECT_EQ(original, url) << spec;
  }
}

TEST_F(RoamexSchemeRewriteTest, NeverTouchesWebSchemes) {
  features_.InitAndEnableFeature(features::kRoamexSchemeAlias);
  GURL url("https://example.com/about");
  const GURL original = url;
  EXPECT_FALSE(MaybeRewriteRoamexAliasURL(&url, nullptr));
  EXPECT_EQ(original, url);
}

// P3: the feature ships disabled — flag-off must be a strict no-op even for
// mapped hosts.
TEST_F(RoamexSchemeRewriteTest, FlagOffIsInert) {
  features_.InitAndDisableFeature(features::kRoamexSchemeAlias);
  GURL url("roamux://about");
  const GURL original = url;
  EXPECT_FALSE(MaybeRewriteRoamexAliasURL(&url, nullptr));
  EXPECT_EQ(original, url);
}

// roam-93 dieback (D2a): the OLD roamex:// scheme is no longer aliased even
// with the feature enabled — the curated map now speaks roamux:// only.
TEST_F(RoamexSchemeRewriteTest, OldRoamexSchemeIsNoLongerAliased) {
  features_.InitAndEnableFeature(features::kRoamexSchemeAlias);
  GURL url("roamex://about");
  const GURL original = url;
  EXPECT_FALSE(MaybeRewriteRoamexAliasURL(&url, nullptr));
  EXPECT_EQ(original, url);
}

} // namespace
} // namespace roamex

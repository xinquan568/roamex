// SPDX-License-Identifier: Apache-2.0
// roam-14 (I-2.5): the extra-data encode/parse round-trip, including the
// lock bit and malformed-input rejection. (TDD: RED before the impl.)

#include "roamux/browser/tabs/tab_initial_url_helper.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace roamux::tabs {
namespace {

TEST(TabInitialUrlPersistenceTest, EncodeDecodeRoundTripsUnlocked) {
  const GURL url("https://example.test/path?q=1");
  const std::string encoded =
      TabInitialUrlHelper::EncodeExtraData(url, /*locked=*/false);
  GURL out;
  bool locked = true;
  ASSERT_TRUE(TabInitialUrlHelper::DecodeExtraData(encoded, &out, &locked));
  EXPECT_EQ(url, out);
  EXPECT_FALSE(locked);
}

TEST(TabInitialUrlPersistenceTest, EncodeDecodeRoundTripsLocked) {
  const GURL url("https://example.test/");
  const std::string encoded =
      TabInitialUrlHelper::EncodeExtraData(url, /*locked=*/true);
  GURL out;
  bool locked = false;
  ASSERT_TRUE(TabInitialUrlHelper::DecodeExtraData(encoded, &out, &locked));
  EXPECT_EQ(url, out);
  EXPECT_TRUE(locked);
}

TEST(TabInitialUrlPersistenceTest, DecodeRejectsMalformed) {
  GURL out;
  bool locked = false;
  // Too short, bad prefix, and an invalid URL all reject without mutating.
  EXPECT_FALSE(TabInitialUrlHelper::DecodeExtraData("", &out, &locked));
  EXPECT_FALSE(TabInitialUrlHelper::DecodeExtraData("x", &out, &locked));
  EXPECT_FALSE(
      TabInitialUrlHelper::DecodeExtraData("2https://x.test/", &out, &locked));
  EXPECT_FALSE(
      TabInitialUrlHelper::DecodeExtraData("1not a url", &out, &locked));
  EXPECT_TRUE(out.is_empty());
}

}  // namespace
}  // namespace roamux::tabs

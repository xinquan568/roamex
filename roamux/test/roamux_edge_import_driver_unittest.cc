// SPDX-License-Identifier: Apache-2.0
// roam-20 (I-3.6): the driver's pure logic — items→carriers split (secrets go
// browser-side, never to the utility import), the Edge-source/flag-gated
// utility mask, and the source-path→app_data_root bridge.

#include "roamux/browser/importer/roamux_edge_import_driver.h"

#include "base/files/file_path.h"
#include "base/test/scoped_feature_list.h"
#include "components/user_data_importer/common/importer_data_types.h"
#include "components/user_data_importer/common/importer_type.h"
#include "roamux/browser/importer/edge_import_adapter.h"
#include "roamux/browser/importer/edge_import_types.h"
#include "roamux/common/roamux_features.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace roamux {
namespace {

TEST(RoamuxEdgeImportDriverPlanTest, StripsSecretsFromUtilityItems) {
  const uint16_t items = user_data_importer::HISTORY |
                         user_data_importer::PASSWORDS |
                         user_data_importer::COOKIES;
  const EdgeImportItemsPlan plan = MakeEdgeImportItemsPlan(items);

  // Non-secret items survive; secrets are removed from the utility mask.
  EXPECT_TRUE(plan.utility_items & user_data_importer::HISTORY);
  EXPECT_FALSE(plan.utility_items & user_data_importer::PASSWORDS);
  EXPECT_FALSE(plan.utility_items & user_data_importer::COOKIES);

  // Secrets requested → their carriers; origin storage is always included.
  EXPECT_TRUE(plan.carriers.contains(EdgeCarrier::kPasswords));
  EXPECT_TRUE(plan.carriers.contains(EdgeCarrier::kCookies));
  EXPECT_TRUE(plan.carriers.contains(EdgeCarrier::kLocalStorage));
  EXPECT_TRUE(plan.carriers.contains(EdgeCarrier::kIndexedDb));
}

TEST(RoamuxEdgeImportDriverPlanTest,
     NoSecretsRequestedStillImportsOriginStorage) {
  const EdgeImportItemsPlan plan =
      MakeEdgeImportItemsPlan(user_data_importer::HISTORY);
  EXPECT_FALSE(plan.carriers.contains(EdgeCarrier::kPasswords));
  EXPECT_FALSE(plan.carriers.contains(EdgeCarrier::kCookies));
  EXPECT_TRUE(plan.carriers.contains(EdgeCarrier::kLocalStorage));
  EXPECT_TRUE(plan.carriers.contains(EdgeCarrier::kIndexedDb));
}

TEST(RoamuxEdgeImportDriverPlanTest, SourceMaskGatedOnEdgeAndFeature) {
  const uint16_t items =
      user_data_importer::HISTORY | user_data_importer::PASSWORDS;
  user_data_importer::SourceProfile edge;
  edge.importer_type = user_data_importer::TYPE_EDGE_CHROMIUM;
  user_data_importer::SourceProfile other;
  other.importer_type = user_data_importer::TYPE_BOOKMARKS_FILE;

  {
    base::test::ScopedFeatureList enabled;
    enabled.InitAndEnableFeature(features::kEdgeImport);
    // Edge + enabled → secrets stripped.
    EXPECT_FALSE(MaskEdgeSecretItemsForUtility(edge, items) &
                 user_data_importer::PASSWORDS);
    // Non-Edge source → unchanged.
    EXPECT_TRUE(MaskEdgeSecretItemsForUtility(other, items) &
                user_data_importer::PASSWORDS);
  }
  {
    base::test::ScopedFeatureList disabled;
    disabled.InitAndDisableFeature(features::kEdgeImport);
    // Feature off → unchanged even for Edge.
    EXPECT_TRUE(MaskEdgeSecretItemsForUtility(edge, items) &
                user_data_importer::PASSWORDS);
  }
}

TEST(RoamuxEdgeImportDriverPlanTest, AppDataRootRoundTripsToProfileDir) {
  const base::FilePath source_path =
      base::FilePath(FILE_PATH_LITERAL("/Users/x/Library/Application Support"))
          .Append(FILE_PATH_LITERAL("Microsoft Edge"))
          .Append(FILE_PATH_LITERAL("Default"));
  const base::FilePath app_data_root =
      AppDataRootFromEdgeProfilePath(source_path);
  // The adapter re-appends "Microsoft Edge/Default", recovering source_path.
  auto adapter = EdgeImportAdapter::Detect(app_data_root);
  EXPECT_EQ(source_path, adapter->profile_dir());
}

}  // namespace
}  // namespace roamux

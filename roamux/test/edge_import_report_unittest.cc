// SPDX-License-Identifier: Apache-2.0
// roam-19 (I-3.5): the best-effort import report aggregation (§5.5).

#include "roamux/browser/importer/edge_import_report.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace roamux {
namespace {

TEST(EdgeImportReportTest, AggregatesCountsAndLookup) {
  EdgeImportReport report;
  report.Add({EdgeCarrier::kPasswords, CarrierStatus::kImported, 3, {}});
  report.Add({EdgeCarrier::kLocalStorage, CarrierStatus::kImported, 2, {}});

  EXPECT_EQ(5u, report.total_imported());
  EXPECT_FALSE(report.any_degraded());
  ASSERT_TRUE(report.Find(EdgeCarrier::kPasswords));
  EXPECT_EQ(3u, report.Find(EdgeCarrier::kPasswords)->count);
  EXPECT_EQ(nullptr, report.Find(EdgeCarrier::kCookies));
}

TEST(EdgeImportReportTest, NonCleanCarrierMarksDegraded) {
  EdgeImportReport report;
  report.Add({EdgeCarrier::kIndexedDb, CarrierStatus::kBlocked, 0,
              "destination IndexedDB already initialized"});
  EXPECT_TRUE(report.any_degraded());
}

TEST(EdgeImportReportTest, SkippedAndImportedAreNotDegraded) {
  EdgeImportReport report;
  report.Add({EdgeCarrier::kIndexedDb, CarrierStatus::kSkipped, 0, {}});
  report.Add({EdgeCarrier::kLocalStorage, CarrierStatus::kImported, 1, {}});
  EXPECT_FALSE(report.any_degraded());
}

TEST(EdgeImportReportTest, StableNames) {
  EXPECT_STREQ("passwords", EdgeCarrierName(EdgeCarrier::kPasswords));
  EXPECT_STREQ("indexedDB", EdgeCarrierName(EdgeCarrier::kIndexedDb));
  EXPECT_STREQ("blocked", CarrierStatusName(CarrierStatus::kBlocked));
  EXPECT_STREQ("feature-disabled",
               CarrierStatusName(CarrierStatus::kFeatureDisabled));
}

}  // namespace
}  // namespace roamux

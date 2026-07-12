// SPDX-License-Identifier: Apache-2.0
// roam-19 (I-3.5): validate-phase preflight — positive running/lock detection
// (SingletonLock symlink), the IndexedDB destination no-clobber gate, and the
// combined ComputeEdgeImportPreflight fact-gathering.

#include "roamux/browser/importer/edge_import_preflight.h"

#include "base/containers/flat_set.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace roamux {
namespace {

TEST(EdgeImportPreflightTest, SingletonLockDetectsRunning) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  EXPECT_FALSE(DetectEdgeRunning(dir.GetPath()).running);

  // Chromium's SingletonLock is a (usually dangling) symlink to "host-pid".
  const base::FilePath lock =
      dir.GetPath().Append(FILE_PATH_LITERAL("SingletonLock"));
  ASSERT_TRUE(base::CreateSymbolicLink(
      base::FilePath(FILE_PATH_LITERAL("myhost-1234")), lock));

  EdgeRunningStatus status = DetectEdgeRunning(dir.GetPath());
  EXPECT_TRUE(status.running);
  EXPECT_EQ("myhost-1234", status.lock_target);
}

TEST(EdgeImportPreflightTest, IndexedDbDestInitializedOnlyWhenNonEmpty) {
  base::ScopedTempDir dest;
  ASSERT_TRUE(dest.CreateUniqueTempDir());
  EXPECT_FALSE(DestCarrierInitialized(dest.GetPath(), EdgeCarrier::kIndexedDb));

  const base::FilePath idb =
      dest.GetPath().Append(FILE_PATH_LITERAL("IndexedDB"));
  ASSERT_TRUE(base::CreateDirectory(idb));
  // An empty IndexedDB/ dir is NOT initialized.
  EXPECT_FALSE(DestCarrierInitialized(dest.GetPath(), EdgeCarrier::kIndexedDb));

  ASSERT_TRUE(base::CreateDirectory(idb.AppendASCII("some_store")));
  EXPECT_TRUE(DestCarrierInitialized(dest.GetPath(), EdgeCarrier::kIndexedDb));
}

TEST(EdgeImportPreflightTest, LiveWriteCarriersAreNotDestGated) {
  base::ScopedTempDir dest;
  ASSERT_TRUE(dest.CreateUniqueTempDir());
  // localStorage (per-key live write) + SQLite carriers are never
  // filesystem-gated.
  const base::FilePath ls = dest.GetPath()
                                .Append(FILE_PATH_LITERAL("Local Storage"))
                                .Append(FILE_PATH_LITERAL("leveldb"));
  ASSERT_TRUE(base::CreateDirectory(ls));
  ASSERT_TRUE(base::WriteFile(ls.AppendASCII("CURRENT"), "x"));
  EXPECT_FALSE(
      DestCarrierInitialized(dest.GetPath(), EdgeCarrier::kLocalStorage));
  EXPECT_FALSE(DestCarrierInitialized(dest.GetPath(), EdgeCarrier::kPasswords));
  EXPECT_FALSE(DestCarrierInitialized(dest.GetPath(), EdgeCarrier::kCookies));
}

TEST(EdgeImportPreflightTest,
     ComputePreflightGathersFactsForRequestedCarriers) {
  base::ScopedTempDir root, dest;
  ASSERT_TRUE(root.CreateUniqueTempDir());
  ASSERT_TRUE(dest.CreateUniqueTempDir());
  const base::FilePath user_data =
      root.GetPath().Append(FILE_PATH_LITERAL("Microsoft Edge"));
  const base::FilePath profile = user_data.Append(FILE_PATH_LITERAL("Default"));
  ASSERT_TRUE(base::CreateDirectory(profile));
  ASSERT_TRUE(base::WriteFile(
      user_data.Append(FILE_PATH_LITERAL("Last Version")), "150.0.0.0"));
  ASSERT_TRUE(base::CreateDirectory(
      profile.Append(FILE_PATH_LITERAL("IndexedDB"))));  // source available

  const base::flat_set<EdgeCarrier> carriers = {EdgeCarrier::kIndexedDb,
                                                EdgeCarrier::kPasswords};
  EdgeImportPreflightResult result =
      ComputeEdgeImportPreflight(root.GetPath(), dest.GetPath(), carriers);

  EXPECT_TRUE(result.version_supported);
  EXPECT_FALSE(result.running.running);
  EXPECT_TRUE(result.SourceAvailable(EdgeCarrier::kIndexedDb));
  EXPECT_FALSE(
      result.SourceAvailable(EdgeCarrier::kPasswords));  // no Login Data
  EXPECT_FALSE(result.DestInitialized(EdgeCarrier::kIndexedDb));
  // A carrier that wasn't requested defaults to false.
  EXPECT_FALSE(result.SourceAvailable(EdgeCarrier::kCookies));
}

}  // namespace
}  // namespace roamux

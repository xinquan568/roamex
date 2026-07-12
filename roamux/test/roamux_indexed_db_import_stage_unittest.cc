// SPDX-License-Identifier: Apache-2.0
// roam-18 (I-3.4): the import stage's copy contract — verbatim per-origin copy,
// no-clobber (a pre-existing / concurrently-appearing destination store is
// never merged into or overwritten), idempotent retry, and legacy
// leveldb+blob pairing — driven directly through Import() on a MayBlock pool.

#include "roamux/browser/importer/roamux_indexed_db_import_stage.h"

#include <string>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "storage/common/database/database_identifier.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace roamux {
namespace {

class RoamuxIndexedDbImportStageTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(edge_.CreateUniqueTempDir());
    ASSERT_TRUE(dest_.CreateUniqueTempDir());
  }

  // The M149 SQLite first-party store dir name for `url`.
  std::string StoreId(const char* url) {
    return storage::GetIdentifierFromOrigin(url::Origin::Create(GURL(url)));
  }

  // Creates a store dir `<edge>/IndexedDB/<name>/` holding one file with
  // `contents`, and returns its path.
  base::FilePath SeedEdgeStore(const std::string& name,
                               const std::string& contents) {
    const base::FilePath store = edge_.GetPath()
                                     .Append(FILE_PATH_LITERAL("IndexedDB"))
                                     .AppendASCII(name);
    EXPECT_TRUE(base::CreateDirectory(store));
    EXPECT_TRUE(base::WriteFile(store.AppendASCII("data"), contents));
    return store;
  }

  base::FilePath DestStore(const std::string& name) {
    return dest_.GetPath()
        .Append(FILE_PATH_LITERAL("IndexedDB"))
        .AppendASCII(name);
  }

  size_t RunImport() {
    RoamuxIndexedDbImportStage stage(edge_.GetPath(), dest_.GetPath());
    base::test::TestFuture<size_t> result;
    stage.Import(result.GetCallback());
    return result.Get();
  }

  std::string ReadFile(const base::FilePath& path) {
    std::string out;
    EXPECT_TRUE(base::ReadFileToString(path, &out));
    return out;
  }

  base::test::TaskEnvironment task_environment_;
  base::ScopedTempDir edge_;
  base::ScopedTempDir dest_;
};

TEST_F(RoamuxIndexedDbImportStageTest, CopiesFirstPartyStoreVerbatim) {
  const std::string id = StoreId("https://idp.test");
  SeedEdgeStore(id, "idb-bytes");

  EXPECT_EQ(1u, RunImport());
  EXPECT_EQ("idb-bytes", ReadFile(DestStore(id).AppendASCII("data")));
  // No staging leftovers under IndexedDB/.
  EXPECT_FALSE(
      base::PathExists(dest_.GetPath()
                           .Append(FILE_PATH_LITERAL("IndexedDB"))
                           .Append(FILE_PATH_LITERAL("roamux-idb-stage-"))));
}

TEST_F(RoamuxIndexedDbImportStageTest, NoClobberLeavesExistingStoreIntact) {
  const std::string id = StoreId("https://idp.test");
  const base::FilePath edge_store = SeedEdgeStore(id, "edge-bytes");
  // A file only present in the Edge store — a merge would leak it into dest.
  ASSERT_TRUE(base::WriteFile(edge_store.AppendASCII("edge-only"), "x"));
  // A destination store for the same origin already exists (fresh-profile
  // precondition violated / a store appeared): it must NOT be merged into or
  // overwritten, and the origin must not be counted.
  ASSERT_TRUE(base::CreateDirectory(DestStore(id)));
  ASSERT_TRUE(base::WriteFile(DestStore(id).AppendASCII("data"), "dest-bytes"));

  EXPECT_EQ(0u, RunImport());
  EXPECT_EQ("dest-bytes", ReadFile(DestStore(id).AppendASCII("data")));
  // The Edge-only sentinel file never leaked in via a merge.
  EXPECT_FALSE(base::PathExists(DestStore(id).AppendASCII("edge-only")));
}

TEST_F(RoamuxIndexedDbImportStageTest, RetriedImportIsIdempotentNoClobber) {
  const std::string id = StoreId("https://idp.test");
  SeedEdgeStore(id, "idb-bytes");

  EXPECT_EQ(1u, RunImport());
  // A second import (retry) finds the published store and skips it, leaving the
  // already-imported bytes untouched — no duplicate, no merge.
  EXPECT_EQ(0u, RunImport());
  EXPECT_EQ("idb-bytes", ReadFile(DestStore(id).AppendASCII("data")));
}

TEST_F(RoamuxIndexedDbImportStageTest, LegacyLevelDbBlobPairPublishTogether) {
  const std::string id = StoreId("https://idp.test");
  SeedEdgeStore(id + ".indexeddb.leveldb", "ldb");
  SeedEdgeStore(id + ".indexeddb.blob", "blob");

  // The pair groups under one origin base and counts once.
  EXPECT_EQ(1u, RunImport());
  EXPECT_EQ("ldb",
            ReadFile(DestStore(id + ".indexeddb.leveldb").AppendASCII("data")));
  EXPECT_EQ("blob",
            ReadFile(DestStore(id + ".indexeddb.blob").AppendASCII("data")));
}

TEST_F(RoamuxIndexedDbImportStageTest, MissingSourceImportsNothing) {
  EXPECT_EQ(0u, RunImport());
}

}  // namespace
}  // namespace roamux

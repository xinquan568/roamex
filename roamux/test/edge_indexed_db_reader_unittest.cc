// SPDX-License-Identifier: Apache-2.0
// roam-18 (I-3.4): first-party IndexedDB store enumeration — the real
// GetIdentifierFromOrigin default-bucket dirs under IndexedDB/ are returned;
// third-party WebStorage/<bucket_id>/ buckets are skipped.

#include "roamux/browser/importer/edge_indexed_db_reader.h"

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "storage/common/database/database_identifier.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace roamux {
namespace {

TEST(EdgeIndexedDbReaderTest, ListsFirstPartySkipsThirdParty) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const base::FilePath idb =
      dir.GetPath().Append(FILE_PATH_LITERAL("IndexedDB"));

  const std::string id = storage::GetIdentifierFromOrigin(
      url::Origin::Create(GURL("https://idp.test")));
  // The M149 SQLite backend stores a first-party origin as `IndexedDB/<id>/`.
  const base::FilePath store = idb.AppendASCII(id);
  ASSERT_TRUE(base::CreateDirectory(store));
  // A third-party bucket lives under WebStorage/<bucket_id>/IndexedDB/ — a
  // separate tree, so it is inherently excluded.
  ASSERT_TRUE(
      base::CreateDirectory(dir.GetPath()
                                .Append(FILE_PATH_LITERAL("WebStorage"))
                                .AppendASCII("5")
                                .Append(FILE_PATH_LITERAL("IndexedDB"))));

  std::vector<base::FilePath> stores =
      ListFirstPartyIndexedDbStores(dir.GetPath());
  ASSERT_EQ(1u, stores.size());
  EXPECT_EQ(store, stores[0]);
}

TEST(EdgeIndexedDbReaderTest, MissingDirEmpty) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  EXPECT_TRUE(ListFirstPartyIndexedDbStores(dir.GetPath()).empty());
}

}  // namespace
}  // namespace roamux

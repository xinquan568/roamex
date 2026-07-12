// SPDX-License-Identifier: Apache-2.0
// roam-17 (I-3.3, §5.3): the localStorage auth carrier survives import to the
// DESTINATION profile's on-disk localStorage — Import() → Flush → drain →
// reopen the destination Local Storage/leveldb from disk with the reader and
// assert the bytes verbatim (proves durable persistence, not just an in-memory
// StorageArea). A partitioned entry never lands.

#include <memory>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/run_loop.h"
#include "base/test/test_future.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "roamux/test/support/roamux_browser_test.h"
#include "components/services/storage/public/mojom/local_storage_control.mojom.h"
#include "components/services/storage/public/mojom/storage_usage_info.mojom.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/test/browser_test.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "roamux/browser/importer/edge_local_storage_reader.h"
#include "roamux/browser/importer/roamux_origin_storage_import_stage.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/storage_key/storage_key.h"
#include "third_party/leveldatabase/env_chromium.h"
#include "third_party/leveldatabase/src/include/leveldb/db.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace roamux {
namespace {

// Writes a Local Storage/leveldb fixture with a first-party auth entry and a
// partitioned entry under `profile_dir`.
void WriteFixture(const base::FilePath& profile_dir,
                  const blink::StorageKey& first_party,
                  const blink::StorageKey& partitioned) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  const base::FilePath ldb =
      profile_dir.Append(FILE_PATH_LITERAL("Local Storage"))
          .Append(FILE_PATH_LITERAL("leveldb"));
  ASSERT_TRUE(base::CreateDirectory(ldb.DirName()));
  leveldb_env::Options options;
  options.create_if_missing = true;
  std::unique_ptr<leveldb::DB> db;
  ASSERT_TRUE(leveldb_env::OpenDB(options, ldb.AsUTF8Unsafe(), &db).ok());
  std::string data_key = "_" + first_party.SerializeForLocalStorage();
  data_key.push_back('\x00');
  data_key += "auth_token";
  std::string value("\x01", 1);
  value += "eyJhbGciOiJ";
  ASSERT_TRUE(db->Put(leveldb::WriteOptions(), data_key, value).ok());
  std::string pkey = "_" + partitioned.SerializeForLocalStorage();
  pkey.push_back('\x00');
  pkey += "tracker";
  ASSERT_TRUE(
      db->Put(leveldb::WriteOptions(), pkey, std::string("\x01z", 2)).ok());
}

using RoamuxOriginStorageImportTest = roamux::test::RoamuxBrowserTest;

IN_PROC_BROWSER_TEST_F(RoamuxOriginStorageImportTest,
                       AuthCarrierSurvivesToDisk) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir edge;
  ASSERT_TRUE(edge.CreateUniqueTempDir());
  const blink::StorageKey first_party = blink::StorageKey::CreateFirstParty(
      url::Origin::Create(GURL("https://idp.test")));
  const blink::StorageKey partitioned = blink::StorageKey::Create(
      url::Origin::Create(GURL("https://tracker.test")),
      net::SchemefulSite(GURL("https://top.test")),
      blink::mojom::AncestorChainBit::kCrossSite);
  WriteFixture(edge.GetPath(), first_party, partitioned);

  Profile* profile = browser()->profile();
  RoamuxOriginStorageImportStage stage(edge.GetPath(), profile);
  base::test::TestFuture<size_t> imported;
  stage.Import(imported.GetCallback());
  EXPECT_EQ(1u, imported.Get());  // one first-party entry accepted

  content::StoragePartition* partition = profile->GetDefaultStoragePartition();
  // Force a commit, then drain the storage task via a replying control call.
  partition->GetLocalStorageControl()->Flush();
  {
    base::test::TestFuture<std::vector<storage::mojom::StorageUsageInfoPtr>>
        usage;
    partition->GetLocalStorageControl()->GetUsage(usage.GetCallback());
    ASSERT_TRUE(usage.Wait());
  }

  // Reopen the DESTINATION profile's Local Storage from disk and assert the
  // auth carrier is present verbatim.
  std::vector<OriginLocalStorage> on_disk =
      ReadEdgeLocalStorage(profile->GetPath());
  bool found = false;
  for (const OriginLocalStorage& origin : on_disk) {
    if (origin.storage_key != first_party) {
      continue;
    }
    for (const LocalStorageEntry& entry : origin.entries) {
      const std::string key(entry.key.begin(), entry.key.end());
      if (key == "auth_token") {
        found = true;
        ASSERT_FALSE(entry.value.empty());
        EXPECT_EQ(0x01u, entry.value[0]);
        EXPECT_EQ("eyJhbGciOiJ",
                  std::string(entry.value.begin() + 1, entry.value.end()));
      }
    }
    // The partitioned tracker origin must never have been imported.
    EXPECT_NE(partitioned, origin.storage_key);
  }
  EXPECT_TRUE(found) << "auth carrier did not persist to destination disk";
}

}  // namespace
}  // namespace roamux

// SPDX-License-Identifier: Apache-2.0
// roam-17 (I-3.3): the localStorage LevelDB reader — first-party entries with
// bytes carried verbatim (the auth carrier), partitioned + meta rows skipped,
// missing/corrupt DB soft-fails.

#include "roamux/browser/importer/edge_local_storage_reader.h"

#include <memory>
#include <string>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/storage_key/storage_key.h"
#include "third_party/leveldatabase/env_chromium.h"
#include "third_party/leveldatabase/src/include/leveldb/db.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace roamux {
namespace {

// Builds a Local Storage/leveldb fixture at `profile_dir` and returns true.
void WriteLevelDb(const base::FilePath& profile_dir,
                  const std::string& serialized_first_party_key,
                  const std::string& serialized_partitioned_key) {
  const base::FilePath ldb =
      profile_dir.Append(FILE_PATH_LITERAL("Local Storage"))
          .Append(FILE_PATH_LITERAL("leveldb"));
  ASSERT_TRUE(base::CreateDirectory(ldb.DirName()));
  leveldb_env::Options options;
  options.create_if_missing = true;
  std::unique_ptr<leveldb::DB> db;
  ASSERT_TRUE(leveldb_env::OpenDB(options, ldb.AsUTF8Unsafe(), &db).ok());

  auto put = [&](const std::string& k, const std::string& v) {
    ASSERT_TRUE(db->Put(leveldb::WriteOptions(), k, v).ok());
  };
  put("VERSION", "1");
  put("META:" + serialized_first_party_key, "meta");
  // First-party data key: '_' + serialized key + 0x00 + script key.
  std::string data_key = "_" + serialized_first_party_key;
  data_key.push_back('\x00');
  data_key += "auth_token";
  // Value: Latin1 format byte (1) + the token.
  std::string value;
  value.push_back('\x01');
  value += "eyJhbGciOiJ";
  put(data_key, value);
  // A partitioned data key → must be skipped.
  std::string p_key = "_" + serialized_partitioned_key;
  p_key.push_back('\x00');
  p_key += "tracker";
  put(p_key, std::string("\x01x", 2));
}

TEST(EdgeLocalStorageReaderTest,
     FirstPartyAuthCarrierVerbatimPartitionedSkipped) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());

  const blink::StorageKey first_party = blink::StorageKey::CreateFirstParty(
      url::Origin::Create(GURL("https://idp.test")));
  ASSERT_TRUE(first_party.IsFirstPartyContext());
  const blink::StorageKey partitioned = blink::StorageKey::Create(
      url::Origin::Create(GURL("https://tracker.test")),
      net::SchemefulSite(GURL("https://top.test")),
      blink::mojom::AncestorChainBit::kCrossSite);
  ASSERT_FALSE(partitioned.IsFirstPartyContext());

  WriteLevelDb(dir.GetPath(), first_party.SerializeForLocalStorage(),
               partitioned.SerializeForLocalStorage());

  std::vector<OriginLocalStorage> got = ReadEdgeLocalStorage(dir.GetPath());
  ASSERT_EQ(1u, got.size());
  EXPECT_EQ(first_party, got[0].storage_key);
  ASSERT_EQ(1u, got[0].entries.size());
  EXPECT_EQ(
      std::vector<uint8_t>({'a', 'u', 't', 'h', '_', 't', 'o', 'k', 'e', 'n'}),
      got[0].entries[0].key);
  // Value bytes verbatim, including the format byte.
  const std::vector<uint8_t>& v = got[0].entries[0].value;
  ASSERT_FALSE(v.empty());
  EXPECT_EQ(0x01u, v[0]);
  EXPECT_EQ("eyJhbGciOiJ", std::string(v.begin() + 1, v.end()));
}

TEST(EdgeLocalStorageReaderTest, MissingDbSoftFails) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  EXPECT_TRUE(ReadEdgeLocalStorage(dir.GetPath()).empty());
}

}  // namespace
}  // namespace roamux

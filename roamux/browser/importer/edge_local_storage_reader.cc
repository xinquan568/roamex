// SPDX-License-Identifier: Apache-2.0
#include "roamux/browser/importer/edge_local_storage_reader.h"

#include <map>
#include <memory>
#include <string>

#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "third_party/leveldatabase/env_chromium.h"
#include "third_party/leveldatabase/src/include/leveldb/db.h"
#include "third_party/leveldatabase/src/include/leveldb/iterator.h"
#include "third_party/leveldatabase/src/include/leveldb/options.h"

namespace roamux {

namespace {

// localStorage data keys are `_<SerializeForLocalStorage(key)>\x00<script
// key>`.
constexpr char kDataPrefix = '_';
constexpr uint8_t kSeparator = 0u;

base::span<const uint8_t> AsSpan(const leveldb::Slice& slice) {
  // SAFETY: leveldb::Slice guarantees data()/size() describe a valid range.
  return UNSAFE_BUFFERS(
      base::span(reinterpret_cast<const uint8_t*>(slice.data()), slice.size()));
}

std::vector<uint8_t> ToBytes(const leveldb::Slice& slice) {
  base::span<const uint8_t> s = AsSpan(slice);
  return std::vector<uint8_t>(s.begin(), s.end());
}

}  // namespace

LocalStorageEntry::LocalStorageEntry() = default;
LocalStorageEntry::LocalStorageEntry(const LocalStorageEntry&) = default;
LocalStorageEntry::LocalStorageEntry(LocalStorageEntry&&) = default;
LocalStorageEntry::~LocalStorageEntry() = default;

OriginLocalStorage::OriginLocalStorage() = default;
OriginLocalStorage::OriginLocalStorage(const OriginLocalStorage&) = default;
OriginLocalStorage::OriginLocalStorage(OriginLocalStorage&&) = default;
OriginLocalStorage::~OriginLocalStorage() = default;

std::vector<OriginLocalStorage> ReadEdgeLocalStorage(
    const base::FilePath& profile_dir) {
  std::vector<OriginLocalStorage> result;
  const base::FilePath source =
      profile_dir.Append(FILE_PATH_LITERAL("Local Storage"))
          .Append(FILE_PATH_LITERAL("leveldb"));
  if (!base::DirectoryExists(source)) {
    return result;
  }
  base::ScopedTempDir temp_dir;
  if (!temp_dir.CreateUniqueTempDir()) {
    return result;
  }
  const base::FilePath copy =
      temp_dir.GetPath().Append(FILE_PATH_LITERAL("leveldb"));
  if (!base::CopyDirectory(source, copy, /*recursive=*/true)) {
    return result;
  }

  // No read-only flag exists; safety comes from operating on the copy.
  leveldb_env::Options options;
  options.create_if_missing = false;
  std::unique_ptr<leveldb::DB> db;
  leveldb::Status status =
      leveldb_env::OpenDB(options, copy.AsUTF8Unsafe(), &db);
  if (!status.ok() || !db) {
    return result;
  }

  // Group entries by storage key (preserving first-seen order).
  std::map<std::string, size_t> index_by_key;
  std::unique_ptr<leveldb::Iterator> it(
      db->NewIterator(leveldb::ReadOptions()));
  for (it->SeekToFirst(); it->Valid(); it->Next()) {
    base::span<const uint8_t> key = AsSpan(it->key());
    if (key.size() < 2 || key[0] != kDataPrefix) {
      continue;  // VERSION / META: / METAACCESS: / anything non-data.
    }
    // Split on the first 0x00 after the '_': left = serialized storage key,
    // right = script key.
    base::span<const uint8_t> after_prefix = key.subspan(1u);
    size_t sep = std::string::npos;
    for (size_t i = 0; i < after_prefix.size(); ++i) {
      if (after_prefix[i] == kSeparator) {
        sep = i;
        break;
      }
    }
    if (sep == std::string::npos) {
      continue;
    }
    base::span<const uint8_t> key_bytes = after_prefix.first(sep);
    base::span<const uint8_t> script_key = after_prefix.subspan(sep + 1);
    const std::string serialized_key(key_bytes.begin(), key_bytes.end());
    std::optional<blink::StorageKey> storage_key =
        blink::StorageKey::DeserializeForLocalStorage(serialized_key);
    if (!storage_key || !storage_key->IsFirstPartyContext()) {
      continue;  // Malformed or partitioned/third-party — skip.
    }
    LocalStorageEntry entry;
    entry.key = std::vector<uint8_t>(script_key.begin(), script_key.end());
    entry.value = ToBytes(it->value());

    auto [iter, inserted] =
        index_by_key.try_emplace(serialized_key, result.size());
    if (inserted) {
      OriginLocalStorage origin;
      origin.storage_key = *storage_key;
      result.push_back(std::move(origin));
    }
    result[iter->second].entries.push_back(std::move(entry));
  }
  if (!it->status().ok()) {
    return {};  // Iterator hit corruption — soft-fail the whole read.
  }
  return result;
}

}  // namespace roamux

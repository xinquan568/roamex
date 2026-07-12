// SPDX-License-Identifier: Apache-2.0
#include "roamux/browser/importer/edge_indexed_db_reader.h"

#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"

namespace roamux {

std::vector<base::FilePath> ListFirstPartyIndexedDbStores(
    const base::FilePath& profile_dir) {
  std::vector<base::FilePath> stores;
  const base::FilePath idb_dir =
      profile_dir.Append(FILE_PATH_LITERAL("IndexedDB"));
  if (!base::DirectoryExists(idb_dir)) {
    return stores;
  }
  // Every directory DIRECTLY under `IndexedDB/` is a first-party store: at this
  // pin the SQLite backend stores `IndexedDB/<GetIdentifierFromOrigin>/…`, and
  // older profiles use the legacy `<id>.indexeddb.leveldb` (+
  // `.indexeddb.blob`) dirs. Third-party / non-default buckets live under a
  // separate `WebStorage/<bucket_id>/IndexedDB/` tree, so scanning only
  // `IndexedDB/*` is inherently first-party-only and backend-agnostic.
  base::FileEnumerator dirs(idb_dir, /*recursive=*/false,
                            base::FileEnumerator::DIRECTORIES);
  for (base::FilePath dir = dirs.Next(); !dir.empty(); dir = dirs.Next()) {
    stores.push_back(dir);
  }
  return stores;
}

}  // namespace roamux

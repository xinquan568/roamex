// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_BROWSER_IMPORTER_EDGE_INDEXED_DB_READER_H_
#define ROAMUX_BROWSER_IMPORTER_EDGE_INDEXED_DB_READER_H_

#include <vector>

#include "base/files/file_path.h"

namespace roamux {

// Enumerates first-party IndexedDB stores in a macOS Chromium-Edge profile
// (roam-18 / I-3.4). Every directory DIRECTLY under `<profile_dir>/IndexedDB/`
// is a first-party default-bucket store: at this pin the SQLite backend uses
// `IndexedDB/<id>/` (where <id> is storage::GetIdentifierFromOrigin(origin)),
// while legacy profiles use `<id>.indexeddb.leveldb` (+ sibling
// `.indexeddb.blob`). Third-party / non-default buckets live under a separate
// `WebStorage/<bucket_id>/IndexedDB/` tree, so scanning only `IndexedDB/*` is
// inherently first-party-only and backend-agnostic. Returns each store's
// directory path (the import stage copies it verbatim, pairing any legacy
// `.indexeddb.blob` sibling). Empty if IndexedDB/ is missing.
std::vector<base::FilePath> ListFirstPartyIndexedDbStores(
    const base::FilePath& profile_dir);

}  // namespace roamux

#endif  // ROAMUX_BROWSER_IMPORTER_EDGE_INDEXED_DB_READER_H_

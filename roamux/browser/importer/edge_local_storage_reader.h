// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_BROWSER_IMPORTER_EDGE_LOCAL_STORAGE_READER_H_
#define ROAMUX_BROWSER_IMPORTER_EDGE_LOCAL_STORAGE_READER_H_

#include <cstdint>
#include <vector>

#include "third_party/blink/public/common/storage_key/storage_key.h"

namespace base {
class FilePath;
}

namespace roamux {

// One localStorage key/value pair, bytes VERBATIM as stored (Edge and Roamux
// share the Blink StorageFormat{UTF16=0,Latin1=1}, so no decode/re-encode).
struct LocalStorageEntry {
  LocalStorageEntry();
  LocalStorageEntry(const LocalStorageEntry&);
  LocalStorageEntry(LocalStorageEntry&&);
  ~LocalStorageEntry();

  std::vector<uint8_t> key;
  std::vector<uint8_t> value;
};

// All first-party localStorage for one origin.
struct OriginLocalStorage {
  OriginLocalStorage();
  OriginLocalStorage(const OriginLocalStorage&);
  OriginLocalStorage(OriginLocalStorage&&);
  ~OriginLocalStorage();

  blink::StorageKey storage_key;
  std::vector<LocalStorageEntry> entries;
};

// Reads first-party localStorage from a macOS Chromium-Edge profile
// (roam-17 / I-3.3). Opens `<profile_dir>/Local Storage/leveldb` via a private
// copy (read-only-by-construction; a live/locked source is never touched),
// keeps only UNPARTITIONED (first-party) storage keys, and carries the script
// key + value bytes verbatim. Returns an empty vector on a missing/locked/
// corrupt DB (soft-fail; roam-19 owns rich negative reporting).
std::vector<OriginLocalStorage> ReadEdgeLocalStorage(
    const base::FilePath& profile_dir);

}  // namespace roamux

#endif  // ROAMUX_BROWSER_IMPORTER_EDGE_LOCAL_STORAGE_READER_H_

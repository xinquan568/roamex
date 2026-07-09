// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMEX_BROWSER_IMPORTER_ROAMEX_INDEXED_DB_IMPORT_STAGE_H_
#define ROAMEX_BROWSER_IMPORTER_ROAMEX_INDEXED_DB_IMPORT_STAGE_H_

#include <cstddef>

#include "base/files/file_path.h"
#include "base/functional/callback.h"

namespace roamex {

// The browser-side first-party IndexedDB import stage (roam-18 / I-3.4):
// verbatim-copies each first-party store directory (the SQLite `<id>/` dir at
// this pin, or a legacy `<id>.indexeddb.leveldb` + sibling `.blob` pair) from
// Edge's IndexedDB/ into the destination profile's IndexedDB/. Edge and Roamex
// share the identical Chromium IndexedDB on-disk format, so a directory copy
// reproduces the store exactly (parsing the internal storage engine is
// infeasible/unsafe).
//
// HARD preconditions (enforced by the import flow, not this stage): the source
// Edge is not running (a live LevelDB is not a consistent snapshot), and the
// destination IndexedDB is uninitialized (fresh profile). Copies are no-clobber
// and truly atomic per store (stage to temp, publish both dirs, roll back on
// any failure). Runs file I/O on a MayBlock thread pool.
class RoamexIndexedDbImportStage {
 public:
  RoamexIndexedDbImportStage(base::FilePath source_path,
                             base::FilePath dest_profile_dir);
  RoamexIndexedDbImportStage(const RoamexIndexedDbImportStage&) = delete;
  RoamexIndexedDbImportStage& operator=(const RoamexIndexedDbImportStage&) =
      delete;
  ~RoamexIndexedDbImportStage();

  // Imports all first-party stores; runs `done(stores)` with the number of
  // fully-published stores.
  void Import(base::OnceCallback<void(size_t stores)> done);

 private:
  const base::FilePath source_path_;
  const base::FilePath dest_profile_dir_;
};

}  // namespace roamex

#endif  // ROAMEX_BROWSER_IMPORTER_ROAMEX_INDEXED_DB_IMPORT_STAGE_H_

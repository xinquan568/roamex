// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_BROWSER_IMPORTER_ROAMUX_ORIGIN_STORAGE_IMPORT_STAGE_H_
#define ROAMUX_BROWSER_IMPORTER_ROAMUX_ORIGIN_STORAGE_IMPORT_STAGE_H_

#include <cstddef>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "roamux/browser/importer/edge_local_storage_reader.h"
#include "third_party/blink/public/mojom/dom_storage/storage_area.mojom.h"

class Profile;

namespace roamux {

// The browser-side first-party localStorage import stage (roam-17 / I-3.3):
// reads Edge's Local Storage LevelDB (copy-to-temp, read-only) and writes the
// entries into the destination profile's localStorage via LocalStorageControl
// / StorageArea::Put (bytes verbatim; the destination store is authoritative).
// Runs entirely browser-side (operator-ruled). Invoked by the import flow
// (roam-20) alongside the other import stages.
class RoamuxOriginStorageImportStage {
 public:
  RoamuxOriginStorageImportStage(base::FilePath source_path, Profile* profile);
  RoamuxOriginStorageImportStage(const RoamuxOriginStorageImportStage&) =
      delete;
  RoamuxOriginStorageImportStage& operator=(
      const RoamuxOriginStorageImportStage&) = delete;
  ~RoamuxOriginStorageImportStage();

  // Imports all first-party localStorage entries; runs `done(accepted)` with
  // the number of Put()s the destination accepted, after all async writes
  // complete.
  void Import(base::OnceCallback<void(size_t accepted)> done);

 private:
  void OnRead(std::vector<OriginLocalStorage> origins);
  void OnPutComplete(bool success);

  const base::FilePath source_path_;
  const raw_ptr<Profile> profile_;
  // StorageArea remotes are held alive until all Puts complete.
  std::vector<mojo::Remote<blink::mojom::StorageArea>> areas_;
  size_t pending_ = 0;
  size_t accepted_ = 0;
  base::OnceCallback<void(size_t)> done_;
};

}  // namespace roamux

#endif  // ROAMUX_BROWSER_IMPORTER_ROAMUX_ORIGIN_STORAGE_IMPORT_STAGE_H_

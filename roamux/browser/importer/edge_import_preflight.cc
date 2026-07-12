// SPDX-License-Identifier: Apache-2.0
#include "roamux/browser/importer/edge_import_preflight.h"

#include <memory>

#include "base/files/file_util.h"
#include "roamux/browser/importer/edge_import_adapter.h"

namespace roamux {

namespace {

// A path-based carrier's destination directory holds data iff it exists and is
// non-empty (a bare/empty dir left by browser init is NOT "initialized").
bool DirHasEntries(const base::FilePath& dir) {
  return base::DirectoryExists(dir) && !base::IsDirectoryEmpty(dir);
}

}  // namespace

EdgeImportPreflightResult::EdgeImportPreflightResult() = default;
EdgeImportPreflightResult::EdgeImportPreflightResult(
    const EdgeImportPreflightResult&) = default;
EdgeImportPreflightResult::EdgeImportPreflightResult(
    EdgeImportPreflightResult&&) = default;
EdgeImportPreflightResult& EdgeImportPreflightResult::operator=(
    EdgeImportPreflightResult&&) = default;
EdgeImportPreflightResult::~EdgeImportPreflightResult() = default;

bool EdgeImportPreflightResult::SourceAvailable(EdgeCarrier carrier) const {
  auto it = source_available.find(carrier);
  return it != source_available.end() && it->second;
}

bool EdgeImportPreflightResult::DestInitialized(EdgeCarrier carrier) const {
  auto it = dest_initialized.find(carrier);
  return it != dest_initialized.end() && it->second;
}

EdgeRunningStatus DetectEdgeRunning(const base::FilePath& user_data_dir) {
  EdgeRunningStatus status;
  const base::FilePath lock =
      user_data_dir.Append(FILE_PATH_LITERAL("SingletonLock"));
  base::FilePath target;
  // ReadSymbolicLink succeeds iff `lock` is a symlink — true even when the
  // target is dangling (Chromium's SingletonLock points at a host-pid string,
  // not a real file), which is exactly the running-Edge case we must catch.
  if (base::ReadSymbolicLink(lock, &target)) {
    status.running = true;
    status.lock_target = target.value();
  }
  return status;
}

bool DestCarrierInitialized(const base::FilePath& dest_profile_dir,
                            EdgeCarrier carrier) {
  switch (carrier) {
    case EdgeCarrier::kIndexedDb:
      // Top-level entries under IndexedDB/ ARE the per-origin stores (the same
      // set the reader enumerates), so a non-empty dir means the destination
      // already holds a store — block to honor the stage's "dest uninitialized"
      // precondition and never clobber/merge.
      return DirHasEntries(
          dest_profile_dir.Append(FILE_PATH_LITERAL("IndexedDB")));
    case EdgeCarrier::kLocalStorage:
      // Not filesystem-gated: <dest>/Local Storage/leveldb is non-empty on any
      // profile the browser ever touched (leveldb infra: CURRENT/MANIFEST/log),
      // so a dir check would false-positive on every live profile. localStorage
      // is a live per-key Put (roam-17): a fresh profile has nothing to
      // clobber, and an existing key is overwritten by design, not corrupted.
      return false;
    case EdgeCarrier::kPasswords:
    case EdgeCarrier::kCookies:
      // SQLite carriers target a fresh profile; not gated on disk here.
      return false;
  }
}

EdgeImportPreflightResult ComputeEdgeImportPreflight(
    const base::FilePath& app_data_root,
    const base::FilePath& dest_profile_dir,
    const base::flat_set<EdgeCarrier>& carriers) {
  std::unique_ptr<EdgeImportAdapter> adapter =
      EdgeImportAdapter::Detect(app_data_root);

  EdgeImportPreflightResult result;
  result.user_data_dir = adapter->user_data_dir();
  result.profile_dir = adapter->profile_dir();
  result.version = adapter->version();
  result.version_supported = adapter->version_supported();
  result.running = DetectEdgeRunning(adapter->user_data_dir());
  for (EdgeCarrier carrier : carriers) {
    result.source_available[carrier] = adapter->CarrierAvailable(carrier);
    result.dest_initialized[carrier] =
        DestCarrierInitialized(dest_profile_dir, carrier);
  }
  return result;
}

}  // namespace roamux

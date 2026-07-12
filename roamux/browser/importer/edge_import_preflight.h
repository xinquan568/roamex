// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_BROWSER_IMPORTER_EDGE_IMPORT_PREFLIGHT_H_
#define ROAMUX_BROWSER_IMPORTER_EDGE_IMPORT_PREFLIGHT_H_

#include <optional>
#include <string>

#include "base/containers/flat_map.h"
#include "base/containers/flat_set.h"
#include "base/files/file_path.h"
#include "base/version.h"
#include "roamux/browser/importer/edge_import_types.h"

namespace roamux {

// The result of the positive "is Edge running?" probe.
struct EdgeRunningStatus {
  bool running = false;
  // The `SingletonLock` symlink target (host-pid) when running; empty
  // otherwise. Diagnostic only — recorded in the report, never parsed for
  // control flow.
  std::string lock_target;
};

// Positive running/lock detection (validate, source side). Chromium's
// ProcessSingleton writes a `SingletonLock` symlink at the User-Data root while
// the browser is running; its presence is a positive "Edge is (or recently was)
// running" signal. A live profile is not a consistent snapshot, so the
// coordinator hard-blocks the snapshot-sensitive IndexedDB carrier and degrades
// the copy-to-temp carriers when this returns running. (A failed copy-to-temp
// read is only a secondary degraded signal, never the primary detector.)
EdgeRunningStatus DetectEdgeRunning(const base::FilePath& user_data_dir);

// Destination no-clobber gate (validate, destination side). roam-19 owns the
// "destination IndexedDB uninitialized" precondition the IndexedDB stage
// delegates to the import flow. Returns true iff the destination already holds
// an IndexedDB store (a non-empty `<dest>/IndexedDB/`, whose top-level entries
// are the per-origin stores) — the coordinator then blocks+reports IndexedDB
// rather than risking a clobber/merge. localStorage is NOT filesystem-gated
// (its leveldb infra is non-empty on any used profile → a dir check would
// always fire; it is a live per-key write, roam-17); passwords/cookies target a
// fresh profile. Both return false.
bool DestCarrierInitialized(const base::FilePath& dest_profile_dir,
                            EdgeCarrier carrier);

// All validate-time facts, computed together off the UI thread (every field
// below is derived from blocking file I/O). `source_available` /
// `dest_initialized` are populated only for the carriers the caller requested.
struct EdgeImportPreflightResult {
  EdgeImportPreflightResult();
  EdgeImportPreflightResult(const EdgeImportPreflightResult&);
  EdgeImportPreflightResult(EdgeImportPreflightResult&&);
  EdgeImportPreflightResult& operator=(EdgeImportPreflightResult&&);
  ~EdgeImportPreflightResult();

  base::FilePath user_data_dir;
  base::FilePath profile_dir;
  std::optional<base::Version> version;
  bool version_supported = false;
  EdgeRunningStatus running;
  base::flat_map<EdgeCarrier, bool> source_available;
  base::flat_map<EdgeCarrier, bool> dest_initialized;

  bool SourceAvailable(EdgeCarrier carrier) const;
  bool DestInitialized(EdgeCarrier carrier) const;
};

// Runs the whole validate phase — version/adapter detection, running/lock
// detection, and per-carrier source-availability + destination-initialized
// checks — with blocking file I/O. MUST be called on a MayBlock sequence (never
// the UI thread). Pure w.r.t. the source (read-only) and the destination (no
// writes).
EdgeImportPreflightResult ComputeEdgeImportPreflight(
    const base::FilePath& app_data_root,
    const base::FilePath& dest_profile_dir,
    const base::flat_set<EdgeCarrier>& carriers);

}  // namespace roamux

#endif  // ROAMUX_BROWSER_IMPORTER_EDGE_IMPORT_PREFLIGHT_H_

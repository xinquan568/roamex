// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMEX_BROWSER_IMPORTER_ROAMEX_EDGE_IMPORT_COORDINATOR_H_
#define ROAMEX_BROWSER_IMPORTER_ROAMEX_EDGE_IMPORT_COORDINATOR_H_

#include <memory>
#include <vector>

#include "base/containers/flat_set.h"
#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "components/services/storage/public/mojom/storage_usage_info.mojom-forward.h"
#include "roamex/browser/importer/edge_import_preflight.h"
#include "roamex/browser/importer/edge_import_report.h"
#include "roamex/browser/importer/edge_import_types.h"

class Profile;
class ProfileWriter;

namespace crypto::apple {
class KeychainV2;
}

namespace roamex {

class RoamexSecretImportStage;
class RoamexOriginStorageImportStage;
class RoamexIndexedDbImportStage;

// The versioned Edge import coordinator (roam-19 / I-3.5) — the "import flow"
// the carrier stages (roam-16 secret, roam-17 localStorage, roam-18 IndexedDB)
// were built to be driven by. It runs the requested browser-side carriers under
// one discipline: flag-gate → detect(version/adapter) → validate(source
// running/ lock + carrier availability, destination uninitialized) →
// per-carrier apply(commit/rollback) → aggregate a best-effort report (§5.5).
//
// Transaction contract (honest): there is NO global two-phase commit. IndexedDB
// is per-origin atomic (its stage stages→atomically-publishes→rolls-back).
// localStorage and passwords/cookies write live and are best-effort /
// no-clobber / corruption-safe on a fresh destination but not atomic. Every
// negative path (Edge running, corrupt source, schema/version mismatch,
// destination already initialized, feature disabled) yields a reduced import +
// a truthful report + an uncorrupted, un-clobbered destination — never an abort
// or a silent loss.
//
// Runs on the UI thread. The caller must keep the coordinator alive until the
// `done` callback runs. Carriers are attempted sequentially; each async stage
// instance is owned by the coordinator across its callback (the stages post
// work and reply through Unretained(this)).
class RoamexEdgeImportCoordinator {
 public:
  // `app_data_root` is the macOS Application-Support root that contains
  // `Microsoft Edge/`. `profile` is the destination (its path is the
  // destination profile dir). `writer` is the ProfileWriter secrets are written
  // through. `carriers` selects which browser-side carriers to import.
  // `keychain_for_testing` is nullptr in production (forwarded to the secret
  // stage).
  RoamexEdgeImportCoordinator(base::FilePath app_data_root,
                              Profile* profile,
                              scoped_refptr<ProfileWriter> writer,
                              base::flat_set<EdgeCarrier> carriers,
                              crypto::apple::KeychainV2* keychain_for_testing);
  RoamexEdgeImportCoordinator(const RoamexEdgeImportCoordinator&) = delete;
  RoamexEdgeImportCoordinator& operator=(const RoamexEdgeImportCoordinator&) =
      delete;
  ~RoamexEdgeImportCoordinator();

  // Runs the import and delivers the report via `done`. Safe to call once.
  void Run(base::OnceCallback<void(EdgeImportReport)> done);

 private:
  void OnPreflightDone(EdgeImportPreflightResult preflight);
  void RunSecretStep();
  void OnSecretDone(bool attempted_passwords,
                    bool attempted_cookies,
                    size_t passwords,
                    size_t cookies,
                    bool keychain_available);
  void RunLocalStorageStep();
  void OnLocalStorageUsageChecked(
      std::vector<storage::mojom::StorageUsageInfoPtr> usage);
  void OnLocalStorageDone(size_t accepted);
  void RunIndexedDbStep();
  void OnIndexedDbDone(size_t stores);
  void Finish();

  bool Requested(EdgeCarrier carrier) const;

  const base::FilePath app_data_root_;
  const raw_ptr<Profile> profile_;
  const base::FilePath dest_profile_dir_;
  const scoped_refptr<ProfileWriter> writer_;
  const base::flat_set<EdgeCarrier> carriers_;
  const raw_ptr<crypto::apple::KeychainV2> keychain_for_testing_;

  EdgeImportPreflightResult preflight_;

  // Each stage is owned for the full duration of its async callback.
  std::unique_ptr<RoamexSecretImportStage> secret_stage_;
  std::unique_ptr<RoamexOriginStorageImportStage> localstorage_stage_;
  std::unique_ptr<RoamexIndexedDbImportStage> indexeddb_stage_;

  EdgeImportReport report_;
  base::OnceCallback<void(EdgeImportReport)> done_;

  base::WeakPtrFactory<RoamexEdgeImportCoordinator> weak_factory_{this};
};

}  // namespace roamex

#endif  // ROAMEX_BROWSER_IMPORTER_ROAMEX_EDGE_IMPORT_COORDINATOR_H_

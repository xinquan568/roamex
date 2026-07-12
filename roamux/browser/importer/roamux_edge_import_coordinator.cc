// SPDX-License-Identifier: Apache-2.0
#include "roamux/browser/importer/roamux_edge_import_coordinator.h"

#include <utility>

#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/importer/profile_writer.h"
#include "chrome/browser/profiles/profile.h"
#include "components/services/storage/public/mojom/local_storage_control.mojom.h"
#include "components/services/storage/public/mojom/storage_usage_info.mojom.h"
#include "components/user_data_importer/common/importer_data_types.h"
#include "content/public/browser/storage_partition.h"
#include "roamux/browser/importer/roamux_indexed_db_import_stage.h"
#include "roamux/browser/importer/roamux_origin_storage_import_stage.h"
#include "roamux/browser/importer/roamux_secret_import_stage.h"
#include "roamux/common/roamux_features.h"

namespace roamux {

RoamuxEdgeImportCoordinator::RoamuxEdgeImportCoordinator(
    base::FilePath app_data_root,
    Profile* profile,
    scoped_refptr<ProfileWriter> writer,
    base::flat_set<EdgeCarrier> carriers,
    crypto::apple::KeychainV2* keychain_for_testing)
    : app_data_root_(std::move(app_data_root)),
      profile_(profile),
      dest_profile_dir_(profile->GetPath()),
      writer_(std::move(writer)),
      carriers_(std::move(carriers)),
      keychain_for_testing_(keychain_for_testing) {}

RoamuxEdgeImportCoordinator::~RoamuxEdgeImportCoordinator() = default;

bool RoamuxEdgeImportCoordinator::Requested(EdgeCarrier carrier) const {
  return carriers_.contains(carrier);
}

void RoamuxEdgeImportCoordinator::Run(
    base::OnceCallback<void(EdgeImportReport)> done) {
  done_ = std::move(done);

  if (!base::FeatureList::IsEnabled(features::kEdgeImport)) {
    for (EdgeCarrier carrier : kAllEdgeCarriers) {
      if (Requested(carrier)) {
        report_.Add({carrier, CarrierStatus::kFeatureDisabled, 0,
                     "roamux::kEdgeImport disabled"});
      }
    }
    Finish();
    return;
  }

  // All validate-time file I/O runs off the UI thread; the stages run on the UI
  // thread once the facts are back.
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&ComputeEdgeImportPreflight, app_data_root_,
                     dest_profile_dir_, carriers_),
      base::BindOnce(&RoamuxEdgeImportCoordinator::OnPreflightDone,
                     weak_factory_.GetWeakPtr()));
}

void RoamuxEdgeImportCoordinator::OnPreflightDone(
    EdgeImportPreflightResult preflight) {
  preflight_ = std::move(preflight);
  report_.source_version = preflight_.version;
  report_.version_supported = preflight_.version_supported;
  report_.edge_running_detected = preflight_.running.running;
  RunSecretStep();
}

void RoamuxEdgeImportCoordinator::RunSecretStep() {
  const bool want_passwords = Requested(EdgeCarrier::kPasswords);
  const bool want_cookies = Requested(EdgeCarrier::kCookies);
  if (!want_passwords && !want_cookies) {
    RunLocalStorageStep();
    return;
  }

  const bool passwords_available =
      want_passwords && preflight_.SourceAvailable(EdgeCarrier::kPasswords);
  const bool cookies_available =
      want_cookies && preflight_.SourceAvailable(EdgeCarrier::kCookies);

  if (want_passwords && !passwords_available) {
    report_.Add({EdgeCarrier::kPasswords, CarrierStatus::kUnsupported, 0,
                 "no source Login Data"});
  }
  if (want_cookies && !cookies_available) {
    report_.Add({EdgeCarrier::kCookies, CarrierStatus::kUnsupported, 0,
                 "no source Cookies"});
  }

  uint16_t items = 0;
  if (passwords_available) {
    items |= user_data_importer::PASSWORDS;
  }
  if (cookies_available) {
    items |= user_data_importer::COOKIES;
  }
  if (items == 0) {
    RunLocalStorageStep();
    return;
  }

  // NOTE (F3 / roam-16 stage design): RoamuxSecretImportStage::Run does its
  // SQLite read + Keychain decrypt synchronously, i.e. blocking work on this UI
  // thread. roam-16 built the stage that way and roam-20 — which owns the live
  // secret-import path plus the roam-16 carry-forward — is responsible for
  // invoking secrets on a fresh profile and, if needed, offloading that work.
  // roam-19 exercises the secret branch only via availability gating and does
  // not add a secret destination no-clobber gate (fresh-profile is roam-20's
  // precondition to uphold for the SQLite carriers).
  secret_stage_ = std::make_unique<RoamuxSecretImportStage>(
      preflight_.profile_dir, writer_, keychain_for_testing_);
  secret_stage_->Run(
      items,
      base::BindOnce(
          [](base::WeakPtr<RoamuxEdgeImportCoordinator> self,
             bool attempted_passwords, bool attempted_cookies,
             RoamuxSecretImportStage::Result result) {
            if (self) {
              self->OnSecretDone(attempted_passwords, attempted_cookies,
                                 result.passwords_imported,
                                 result.cookies_imported,
                                 result.keychain_available);
            }
          },
          weak_factory_.GetWeakPtr(), passwords_available, cookies_available));
}

void RoamuxEdgeImportCoordinator::OnSecretDone(bool attempted_passwords,
                                               bool attempted_cookies,
                                               size_t passwords,
                                               size_t cookies,
                                               bool keychain_available) {
  auto status_for = [&](size_t count) -> std::pair<CarrierStatus, std::string> {
    if (!keychain_available) {
      return {CarrierStatus::kDegraded,
              "Edge keychain unavailable — secrets not imported"};
    }
    if (preflight_.running.running) {
      return {CarrierStatus::kDegraded,
              "Edge was running; secrets imported best-effort"};
    }
    if (count == 0) {
      // Source was present (availability was checked) but nothing imported —
      // possibly empty, possibly an unreadable/corrupt source. Report it as a
      // reduced import, never a silent clean skip (F2).
      return {CarrierStatus::kDegraded,
              "source present but no entries imported (empty or unreadable)"};
    }
    return {CarrierStatus::kImported, std::string()};
  };

  if (attempted_passwords) {
    auto [status, reason] = status_for(passwords);
    report_.Add(
        {EdgeCarrier::kPasswords, status, passwords, std::move(reason)});
  }
  if (attempted_cookies) {
    auto [status, reason] = status_for(cookies);
    report_.Add({EdgeCarrier::kCookies, status, cookies, std::move(reason)});
  }
  secret_stage_.reset();
  RunLocalStorageStep();
}

void RoamuxEdgeImportCoordinator::RunLocalStorageStep() {
  if (!Requested(EdgeCarrier::kLocalStorage)) {
    RunIndexedDbStep();
    return;
  }
  if (!preflight_.SourceAvailable(EdgeCarrier::kLocalStorage)) {
    report_.Add({EdgeCarrier::kLocalStorage, CarrierStatus::kUnsupported, 0,
                 "no source localStorage"});
    RunIndexedDbStep();
    return;
  }
  // Destination no-clobber (F1). localStorage writes live per-key (roam-17), so
  // a filesystem gate on `Local Storage/leveldb` is unsound (its infra is
  // present on any used profile). Instead query the LIVE store for actual data:
  // if the destination already holds ANY localStorage, it is not a fresh import
  // target — block + report rather than overwrite existing entries.
  profile_->GetDefaultStoragePartition()->GetLocalStorageControl()->GetUsage(
      base::BindOnce(&RoamuxEdgeImportCoordinator::OnLocalStorageUsageChecked,
                     weak_factory_.GetWeakPtr()));
}

void RoamuxEdgeImportCoordinator::OnLocalStorageUsageChecked(
    std::vector<storage::mojom::StorageUsageInfoPtr> usage) {
  if (!usage.empty()) {
    report_.Add({EdgeCarrier::kLocalStorage, CarrierStatus::kBlocked, 0,
                 "destination localStorage already initialized"});
    RunIndexedDbStep();
    return;
  }
  localstorage_stage_ = std::make_unique<RoamuxOriginStorageImportStage>(
      preflight_.profile_dir, profile_);
  localstorage_stage_->Import(
      base::BindOnce(&RoamuxEdgeImportCoordinator::OnLocalStorageDone,
                     weak_factory_.GetWeakPtr()));
}

void RoamuxEdgeImportCoordinator::OnLocalStorageDone(size_t accepted) {
  CarrierStatus status;
  std::string reason;
  if (preflight_.running.running) {
    status = CarrierStatus::kDegraded;
    reason = "Edge was running; localStorage imported best-effort";
  } else if (accepted > 0) {
    status = CarrierStatus::kImported;
  } else {
    // Source present but nothing imported — reduced import, not a silent skip
    // (F2).
    status = CarrierStatus::kDegraded;
    reason =
        "source present but no localStorage entries imported (empty or "
        "unreadable)";
  }
  report_.Add(
      {EdgeCarrier::kLocalStorage, status, accepted, std::move(reason)});
  localstorage_stage_.reset();
  RunIndexedDbStep();
}

void RoamuxEdgeImportCoordinator::RunIndexedDbStep() {
  if (!Requested(EdgeCarrier::kIndexedDb)) {
    Finish();
    return;
  }
  if (!preflight_.SourceAvailable(EdgeCarrier::kIndexedDb)) {
    report_.Add({EdgeCarrier::kIndexedDb, CarrierStatus::kUnsupported, 0,
                 "no source IndexedDB"});
    Finish();
    return;
  }
  // Hard block: a running Edge is not a consistent IndexedDB snapshot (the
  // stage header requires Edge-not-running). Skip rather than risk a torn copy.
  if (preflight_.running.running) {
    report_.Add({EdgeCarrier::kIndexedDb, CarrierStatus::kBlocked, 0,
                 "Edge is running; IndexedDB snapshot is unsafe"});
    Finish();
    return;
  }
  if (preflight_.DestInitialized(EdgeCarrier::kIndexedDb)) {
    report_.Add({EdgeCarrier::kIndexedDb, CarrierStatus::kBlocked, 0,
                 "destination IndexedDB already initialized"});
    Finish();
    return;
  }

  indexeddb_stage_ = std::make_unique<RoamuxIndexedDbImportStage>(
      preflight_.profile_dir, dest_profile_dir_);
  indexeddb_stage_->Import(
      base::BindOnce(&RoamuxEdgeImportCoordinator::OnIndexedDbDone,
                     weak_factory_.GetWeakPtr()));
}

void RoamuxEdgeImportCoordinator::OnIndexedDbDone(size_t stores) {
  // Source was present (availability was checked); zero stores published means
  // an empty or unreadable source — a reduced import, not a clean skip (F2).
  report_.Add(
      {EdgeCarrier::kIndexedDb,
       stores > 0 ? CarrierStatus::kImported : CarrierStatus::kDegraded, stores,
       stores > 0 ? std::string()
                  : "source present but no first-party stores imported"});
  indexeddb_stage_.reset();
  Finish();
}

void RoamuxEdgeImportCoordinator::Finish() {
  std::move(done_).Run(std::move(report_));
}

}  // namespace roamux

// SPDX-License-Identifier: Apache-2.0
#include "roamex/browser/importer/roamex_edge_import_coordinator.h"

#include <utility>

#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/importer/profile_writer.h"
#include "chrome/browser/profiles/profile.h"
#include "components/user_data_importer/common/importer_data_types.h"
#include "roamex/browser/importer/roamex_indexed_db_import_stage.h"
#include "roamex/browser/importer/roamex_origin_storage_import_stage.h"
#include "roamex/browser/importer/roamex_secret_import_stage.h"
#include "roamex/common/roamex_features.h"

namespace roamex {

RoamexEdgeImportCoordinator::RoamexEdgeImportCoordinator(
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

RoamexEdgeImportCoordinator::~RoamexEdgeImportCoordinator() = default;

bool RoamexEdgeImportCoordinator::Requested(EdgeCarrier carrier) const {
  return carriers_.contains(carrier);
}

void RoamexEdgeImportCoordinator::Run(
    base::OnceCallback<void(EdgeImportReport)> done) {
  done_ = std::move(done);

  if (!base::FeatureList::IsEnabled(features::kEdgeImport)) {
    for (EdgeCarrier carrier : kAllEdgeCarriers) {
      if (Requested(carrier)) {
        report_.Add({carrier, CarrierStatus::kFeatureDisabled, 0,
                     "roamex::kEdgeImport disabled"});
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
      base::BindOnce(&RoamexEdgeImportCoordinator::OnPreflightDone,
                     weak_factory_.GetWeakPtr()));
}

void RoamexEdgeImportCoordinator::OnPreflightDone(
    EdgeImportPreflightResult preflight) {
  preflight_ = std::move(preflight);
  report_.source_version = preflight_.version;
  report_.version_supported = preflight_.version_supported;
  report_.edge_running_detected = preflight_.running.running;
  RunSecretStep();
}

void RoamexEdgeImportCoordinator::RunSecretStep() {
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

  secret_stage_ = std::make_unique<RoamexSecretImportStage>(
      preflight_.profile_dir, writer_, keychain_for_testing_);
  secret_stage_->Run(
      items,
      base::BindOnce(
          [](base::WeakPtr<RoamexEdgeImportCoordinator> self,
             bool attempted_passwords, bool attempted_cookies,
             RoamexSecretImportStage::Result result) {
            if (self) {
              self->OnSecretDone(attempted_passwords, attempted_cookies,
                                 result.passwords_imported,
                                 result.cookies_imported,
                                 result.keychain_available);
            }
          },
          weak_factory_.GetWeakPtr(), passwords_available, cookies_available));
}

void RoamexEdgeImportCoordinator::OnSecretDone(bool attempted_passwords,
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
      return {CarrierStatus::kSkipped, "no entries imported"};
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

void RoamexEdgeImportCoordinator::RunLocalStorageStep() {
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
  // localStorage is a live per-key write (roam-17); it is not whole-carrier
  // dest-gated (see DestCarrierInitialized). A fresh profile has nothing to
  // clobber; an existing key is overwritten by design.

  localstorage_stage_ = std::make_unique<RoamexOriginStorageImportStage>(
      preflight_.profile_dir, profile_);
  localstorage_stage_->Import(
      base::BindOnce(&RoamexEdgeImportCoordinator::OnLocalStorageDone,
                     weak_factory_.GetWeakPtr()));
}

void RoamexEdgeImportCoordinator::OnLocalStorageDone(size_t accepted) {
  CarrierStatus status;
  std::string reason;
  if (preflight_.running.running) {
    status = CarrierStatus::kDegraded;
    reason = "Edge was running; localStorage imported best-effort";
  } else if (accepted > 0) {
    status = CarrierStatus::kImported;
  } else {
    status = CarrierStatus::kSkipped;
    reason = "no localStorage entries imported";
  }
  report_.Add(
      {EdgeCarrier::kLocalStorage, status, accepted, std::move(reason)});
  localstorage_stage_.reset();
  RunIndexedDbStep();
}

void RoamexEdgeImportCoordinator::RunIndexedDbStep() {
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

  indexeddb_stage_ = std::make_unique<RoamexIndexedDbImportStage>(
      preflight_.profile_dir, dest_profile_dir_);
  indexeddb_stage_->Import(
      base::BindOnce(&RoamexEdgeImportCoordinator::OnIndexedDbDone,
                     weak_factory_.GetWeakPtr()));
}

void RoamexEdgeImportCoordinator::OnIndexedDbDone(size_t stores) {
  report_.Add({EdgeCarrier::kIndexedDb,
               stores > 0 ? CarrierStatus::kImported : CarrierStatus::kSkipped,
               stores, stores > 0 ? std::string() : "no first-party stores"});
  indexeddb_stage_.reset();
  Finish();
}

void RoamexEdgeImportCoordinator::Finish() {
  std::move(done_).Run(std::move(report_));
}

}  // namespace roamex

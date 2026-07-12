// SPDX-License-Identifier: Apache-2.0
#include "roamux/browser/importer/roamux_edge_import_driver.h"

#include <utility>

#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/memory/scoped_refptr.h"
#include "chrome/browser/importer/profile_writer.h"
#include "components/user_data_importer/common/importer_data_types.h"
#include "components/user_data_importer/common/importer_type.h"
#include "roamux/browser/importer/roamux_edge_import_coordinator.h"
#include "roamux/common/roamux_features.h"

namespace roamux {

namespace {

bool IsEdgeSource(const user_data_importer::SourceProfile& source_profile) {
  return source_profile.importer_type == user_data_importer::TYPE_EDGE_CHROMIUM;
}

}  // namespace

EdgeImportItemsPlan::EdgeImportItemsPlan() = default;
EdgeImportItemsPlan::EdgeImportItemsPlan(const EdgeImportItemsPlan&) = default;
EdgeImportItemsPlan::EdgeImportItemsPlan(EdgeImportItemsPlan&&) = default;
EdgeImportItemsPlan::~EdgeImportItemsPlan() = default;

EdgeImportItemsPlan MakeEdgeImportItemsPlan(uint16_t items) {
  EdgeImportItemsPlan plan;
  // Secrets are imported browser-side (roam-16) — never handed to the utility
  // process — so strip them from the utility mask.
  plan.utility_items =
      items & ~static_cast<uint16_t>(user_data_importer::PASSWORDS |
                                     user_data_importer::COOKIES);
  if (items & user_data_importer::PASSWORDS) {
    plan.carriers.insert(EdgeCarrier::kPasswords);
  }
  if (items & user_data_importer::COOKIES) {
    plan.carriers.insert(EdgeCarrier::kCookies);
  }
  // The E3 origin-storage carriers are always part of an Edge import.
  plan.carriers.insert(EdgeCarrier::kLocalStorage);
  plan.carriers.insert(EdgeCarrier::kIndexedDb);
  return plan;
}

uint16_t MaskEdgeSecretItemsForUtility(uint16_t items) {
  return MakeEdgeImportItemsPlan(items).utility_items;
}

uint16_t MaskEdgeSecretItemsForUtility(
    const user_data_importer::SourceProfile& source_profile,
    uint16_t items) {
  if (!IsEdgeSource(source_profile) ||
      !base::FeatureList::IsEnabled(features::kEdgeImport)) {
    return items;
  }
  return MaskEdgeSecretItemsForUtility(items);
}

base::FilePath AppDataRootFromEdgeProfilePath(
    const base::FilePath& source_path) {
  // source_path == <app_data_root>/Microsoft Edge/Default.
  return source_path.DirName().DirName();
}

RoamuxEdgeImportDriver::RoamuxEdgeImportDriver(
    Profile* profile,
    base::FilePath app_data_root,
    uint16_t items,
    crypto::apple::KeychainV2* keychain_for_testing)
    : profile_(profile),
      app_data_root_(std::move(app_data_root)),
      items_(items),
      keychain_for_testing_(keychain_for_testing) {}

RoamuxEdgeImportDriver::~RoamuxEdgeImportDriver() = default;

void RoamuxEdgeImportDriver::Start(
    base::OnceCallback<void(EdgeImportReport)> done) {
  done_ = std::move(done);
  const EdgeImportItemsPlan plan = MakeEdgeImportItemsPlan(items_);

  if (!base::FeatureList::IsEnabled(features::kEdgeImport)) {
    EdgeImportReport report;
    for (EdgeCarrier carrier : plan.carriers) {
      report.Add({carrier, CarrierStatus::kFeatureDisabled, 0,
                  "roamux::kEdgeImport disabled"});
    }
    std::move(done_).Run(std::move(report));
    return;
  }

  coordinator_ = std::make_unique<RoamuxEdgeImportCoordinator>(
      app_data_root_, profile_, base::MakeRefCounted<ProfileWriter>(profile_),
      plan.carriers, keychain_for_testing_);
  coordinator_->Run(base::BindOnce(&RoamuxEdgeImportDriver::OnCoordinatorDone,
                                   weak_factory_.GetWeakPtr()));
}

void RoamuxEdgeImportDriver::OnCoordinatorDone(EdgeImportReport report) {
  // Do not destroy `coordinator_` from within its own callback — it is torn
  // down when this driver is destroyed by its owner.
  std::move(done_).Run(std::move(report));
}

bool MaybeStartEdgeBrowserSideImport(
    const user_data_importer::SourceProfile& source_profile,
    Profile* target_profile,
    uint16_t items,
    base::OnceClosure on_done) {
  if (!IsEdgeSource(source_profile) ||
      !base::FeatureList::IsEnabled(features::kEdgeImport)) {
    return false;
  }
  auto driver = std::make_unique<RoamuxEdgeImportDriver>(
      target_profile,
      AppDataRootFromEdgeProfilePath(source_profile.source_path), items,
      /*keychain_for_testing=*/nullptr);
  RoamuxEdgeImportDriver* driver_raw = driver.get();
  // Keep the driver alive across its async run by owning it in the completion
  // callback; it is destroyed after `on_done` runs.
  driver_raw->Start(base::BindOnce(
      [](std::unique_ptr<RoamuxEdgeImportDriver> owned, base::OnceClosure done,
         EdgeImportReport report) { std::move(done).Run(); },
      std::move(driver), std::move(on_done)));
  return true;
}

}  // namespace roamux

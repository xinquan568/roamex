// SPDX-License-Identifier: Apache-2.0
#include "roamux/browser/importer/edge_import_report.h"

namespace roamux {

const char* EdgeCarrierName(EdgeCarrier carrier) {
  switch (carrier) {
    case EdgeCarrier::kPasswords:
      return "passwords";
    case EdgeCarrier::kCookies:
      return "cookies";
    case EdgeCarrier::kLocalStorage:
      return "localStorage";
    case EdgeCarrier::kIndexedDb:
      return "indexedDB";
  }
}

const char* CarrierStatusName(CarrierStatus status) {
  switch (status) {
    case CarrierStatus::kImported:
      return "imported";
    case CarrierStatus::kSkipped:
      return "skipped";
    case CarrierStatus::kBlocked:
      return "blocked";
    case CarrierStatus::kFailed:
      return "failed";
    case CarrierStatus::kDegraded:
      return "degraded";
    case CarrierStatus::kUnsupported:
      return "unsupported";
    case CarrierStatus::kFeatureDisabled:
      return "feature-disabled";
  }
}

EdgeImportReport::EdgeImportReport() = default;
EdgeImportReport::EdgeImportReport(EdgeImportReport&&) = default;
EdgeImportReport& EdgeImportReport::operator=(EdgeImportReport&&) = default;
EdgeImportReport::~EdgeImportReport() = default;

void EdgeImportReport::Add(CarrierOutcome outcome) {
  carriers.push_back(std::move(outcome));
}

const CarrierOutcome* EdgeImportReport::Find(EdgeCarrier carrier) const {
  for (const CarrierOutcome& outcome : carriers) {
    if (outcome.carrier == carrier) {
      return &outcome;
    }
  }
  return nullptr;
}

size_t EdgeImportReport::total_imported() const {
  size_t total = 0;
  for (const CarrierOutcome& outcome : carriers) {
    total += outcome.count;
  }
  return total;
}

bool EdgeImportReport::any_degraded() const {
  for (const CarrierOutcome& outcome : carriers) {
    switch (outcome.status) {
      case CarrierStatus::kImported:
      case CarrierStatus::kSkipped:
        break;
      case CarrierStatus::kBlocked:
      case CarrierStatus::kFailed:
      case CarrierStatus::kDegraded:
      case CarrierStatus::kUnsupported:
      case CarrierStatus::kFeatureDisabled:
        return true;
    }
  }
  return false;
}

}  // namespace roamux

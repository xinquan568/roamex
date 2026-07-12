// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_BROWSER_IMPORTER_EDGE_IMPORT_REPORT_H_
#define ROAMUX_BROWSER_IMPORTER_EDGE_IMPORT_REPORT_H_

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "base/version.h"
#include "roamux/browser/importer/edge_import_types.h"

namespace roamux {

// The outcome of one carrier in an Edge import. `count` is what the destination
// actually accepted (passwords/cookies/localStorage entries, or IndexedDB
// stores); `reason` is a short human-readable note for degraded/blocked/failed
// carriers (empty for a clean import).
enum class CarrierStatus {
  kImported,         // fully imported (count is the amount)
  kSkipped,          // nothing to import (no source data)
  kBlocked,          // precondition failed → not attempted (no corruption)
  kFailed,           // attempted but failed (destination left uncorrupted)
  kDegraded,         // partially imported / imported under a caveat
  kUnsupported,      // source carrier absent or schema-mismatched
  kFeatureDisabled,  // roamux::kEdgeImport was off
};

const char* CarrierStatusName(CarrierStatus status);

struct CarrierOutcome {
  EdgeCarrier carrier;
  CarrierStatus status;
  size_t count = 0;
  std::string reason;
};

// The aggregate, best-effort report (§5.5) the coordinator produces. It never
// claims a global transaction: each carrier reports its own outcome, so a
// partial import is a reported reduced success, never a silent loss.
class EdgeImportReport {
 public:
  EdgeImportReport();
  EdgeImportReport(const EdgeImportReport&) = delete;
  EdgeImportReport& operator=(const EdgeImportReport&) = delete;
  EdgeImportReport(EdgeImportReport&&);
  EdgeImportReport& operator=(EdgeImportReport&&);
  ~EdgeImportReport();

  // The detected source Edge version (nullopt = undetermined), and whether it
  // is within the milestone family this build's adapter supports.
  std::optional<base::Version> source_version;
  bool version_supported = false;
  // True if a running Edge instance was detected at validate time.
  bool edge_running_detected = false;

  std::vector<CarrierOutcome> carriers;

  void Add(CarrierOutcome outcome);
  const CarrierOutcome* Find(EdgeCarrier carrier) const;

  // Total items/stores imported across all carriers.
  size_t total_imported() const;
  // True if any carrier was not a clean kImported/kSkipped — i.e. the user
  // should be told the import was reduced.
  bool any_degraded() const;
};

}  // namespace roamux

#endif  // ROAMUX_BROWSER_IMPORTER_EDGE_IMPORT_REPORT_H_

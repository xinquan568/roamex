// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMEX_UTILITY_IMPORTER_ROAMEX_EDGE_IMPORTER_H_
#define ROAMEX_UTILITY_IMPORTER_ROAMEX_EDGE_IMPORTER_H_

#include "chrome/utility/importer/importer.h"
#include "components/user_data_importer/common/importer_data_types.h"

namespace roamex {

// The utility-process importer glue for a macOS Chromium-Edge profile
// (roam-15 / I-3.1). Thin: it owns the NotifyStarted/Item/Ended envelope and
// routes EdgeProfileReader output to the ImporterBridge. Compiled ONLY into
// //chrome/utility (which owns Importer's out-of-line members); registered via
// patch 0013's CreateImporterByType case.
class RoamexEdgeImporter : public Importer {
 public:
  RoamexEdgeImporter();
  RoamexEdgeImporter(const RoamexEdgeImporter&) = delete;
  RoamexEdgeImporter& operator=(const RoamexEdgeImporter&) = delete;

  // Importer:
  void StartImport(const user_data_importer::SourceProfile& source_profile,
                   uint16_t items,
                   ImporterBridge* bridge) override;

 private:
  ~RoamexEdgeImporter() override;
};

}  // namespace roamex

#endif  // ROAMEX_UTILITY_IMPORTER_ROAMEX_EDGE_IMPORTER_H_

// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_UTILITY_IMPORTER_ROAMUX_EDGE_IMPORTER_H_
#define ROAMUX_UTILITY_IMPORTER_ROAMUX_EDGE_IMPORTER_H_

#include "chrome/utility/importer/importer.h"
#include "components/user_data_importer/common/importer_data_types.h"

namespace roamux {

// The utility-process importer glue for a macOS Chromium-Edge profile
// (roam-15 / I-3.1). Thin: it owns the NotifyStarted/Item/Ended envelope and
// routes EdgeProfileReader output to the ImporterBridge. Compiled ONLY into
// //chrome/utility (which owns Importer's out-of-line members); registered via
// patch 0013's CreateImporterByType case.
class RoamuxEdgeImporter : public Importer {
 public:
  RoamuxEdgeImporter();
  RoamuxEdgeImporter(const RoamuxEdgeImporter&) = delete;
  RoamuxEdgeImporter& operator=(const RoamuxEdgeImporter&) = delete;

  // Importer:
  void StartImport(const user_data_importer::SourceProfile& source_profile,
                   uint16_t items,
                   ImporterBridge* bridge) override;

 private:
  ~RoamuxEdgeImporter() override;
};

}  // namespace roamux

#endif  // ROAMUX_UTILITY_IMPORTER_ROAMUX_EDGE_IMPORTER_H_

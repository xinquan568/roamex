// SPDX-License-Identifier: Apache-2.0
#include "roamux/utility/importer/roamux_edge_importer.h"

#include <vector>

#include "base/strings/utf_string_conversions.h"
#include "chrome/common/importer/importer_bridge.h"
#include "components/user_data_importer/common/imported_bookmark_entry.h"
#include "components/user_data_importer/common/importer_url_row.h"
#include "roamux/utility/importer/edge_profile_reader.h"

namespace roamux {

RoamuxEdgeImporter::RoamuxEdgeImporter() = default;
RoamuxEdgeImporter::~RoamuxEdgeImporter() = default;

void RoamuxEdgeImporter::StartImport(
    const user_data_importer::SourceProfile& source_profile,
    uint16_t items,
    ImporterBridge* bridge) {
  bridge_ = bridge;
  bridge_->NotifyStarted();

  EdgeProfileReader reader(source_profile.source_path);

  if (items & user_data_importer::HISTORY) {
    bridge_->NotifyItemStarted(user_data_importer::HISTORY);
    std::vector<user_data_importer::ImporterURLRow> rows = reader.ReadHistory();
    if (!rows.empty()) {
      bridge_->SetHistoryItems(rows,
                               user_data_importer::VISIT_SOURCE_EDGE_IMPORTED);
    }
    bridge_->NotifyItemEnded(user_data_importer::HISTORY);
  }

  if (items & user_data_importer::FAVORITES) {
    bridge_->NotifyItemStarted(user_data_importer::FAVORITES);
    std::vector<user_data_importer::ImportedBookmarkEntry> bookmarks =
        reader.ReadBookmarks();
    if (!bookmarks.empty()) {
      bridge_->AddBookmarks(bookmarks, u"Imported from Microsoft Edge");
    }
    bridge_->NotifyItemEnded(user_data_importer::FAVORITES);
  }

  if (items & user_data_importer::SEARCH_ENGINES) {
    bridge_->NotifyItemStarted(user_data_importer::SEARCH_ENGINES);
    std::vector<user_data_importer::SearchEngineInfo> engines =
        reader.ReadSearchEngines();
    if (!engines.empty()) {
      bridge_->SetKeywords(engines, /*unique_on_host_and_path=*/false);
    }
    bridge_->NotifyItemEnded(user_data_importer::SEARCH_ENGINES);
  }

  if (items & user_data_importer::AUTOFILL_FORM_DATA) {
    bridge_->NotifyItemStarted(user_data_importer::AUTOFILL_FORM_DATA);
    std::vector<ImporterAutofillFormDataEntry> autofill = reader.ReadAutofill();
    if (!autofill.empty()) {
      bridge_->SetAutofillFormData(autofill);
    }
    bridge_->NotifyItemEnded(user_data_importer::AUTOFILL_FORM_DATA);
  }

  bridge_->NotifyEnded();
}

}  // namespace roamux

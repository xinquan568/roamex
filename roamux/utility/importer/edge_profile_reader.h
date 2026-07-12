// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_UTILITY_IMPORTER_EDGE_PROFILE_READER_H_
#define ROAMUX_UTILITY_IMPORTER_EDGE_PROFILE_READER_H_

#include <optional>
#include <vector>

#include "chrome/common/importer/importer_autofill_form_data_entry.h"
#include "components/user_data_importer/common/imported_bookmark_entry.h"
#include "components/user_data_importer/common/importer_data_types.h"
#include "components/user_data_importer/common/importer_url_row.h"

namespace base {
class FilePath;
}

namespace roamux {

// Given a macOS Application-Support root (from DIR_APP_DATA — overridable in
// tests), returns the SourceProfile for a Chromium-Edge default profile if it
// exists, advertising only the NON-SECRET items (passwords/cookies are
// roam-16). Pure/hermetic: no flag check, no PathService — the caller
// (importer_list.cc) owns the kEdgeImport gate. Returns nullopt if absent.
std::optional<user_data_importer::SourceProfile> DetectEdgeSourceProfile(
    const base::FilePath& app_data_root);

// Pure, side-effect-free reader for a macOS Chromium-Edge (150.x) profile
// directory (roam-15 / I-3.1). It reads only the NON-SECRET hard-guaranteed
// set — history, bookmarks, search engines, autofill form data — into the
// Chromium importer-framework structs. Passwords/cookies (roam-16) and origin
// storage (roam-17/18) are out of scope here.
//
// SECURITY (I-3.1 §5, Step-2 finding 2): the ProfileImport utility service is
// kNoSandbox at this pin, so confinement is our responsibility. Every SQLite
// database is COPIED to a private temp file and opened there — the source
// profile is opened read-only-by-construction (never touched), and a live/
// locked Edge DB cannot corrupt us. No network, no Keychain.
class EdgeProfileReader {
 public:
  explicit EdgeProfileReader(const base::FilePath& profile_dir);
  EdgeProfileReader(const EdgeProfileReader&) = delete;
  EdgeProfileReader& operator=(const EdgeProfileReader&) = delete;
  ~EdgeProfileReader();

  // Each returns an empty vector (never crashes) on a missing/locked/malformed
  // source — rich negative reporting is roam-19's versioned adapter.
  std::vector<user_data_importer::ImporterURLRow> ReadHistory() const;
  std::vector<user_data_importer::ImportedBookmarkEntry> ReadBookmarks() const;
  std::vector<user_data_importer::SearchEngineInfo> ReadSearchEngines() const;
  std::vector<ImporterAutofillFormDataEntry> ReadAutofill() const;

 private:
  const base::FilePath profile_dir_;
};

}  // namespace roamux

#endif  // ROAMUX_UTILITY_IMPORTER_EDGE_PROFILE_READER_H_

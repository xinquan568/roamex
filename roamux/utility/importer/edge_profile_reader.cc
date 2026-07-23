// SPDX-License-Identifier: Apache-2.0
#include "roamux/utility/importer/edge_profile_reader.h"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/strings/string_util.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "url/gurl.h"

namespace roamux {

namespace {

constexpr sql::Database::Tag kReaderTag{"RoamuxEdgeImporter"};

// Chromium/Edge History + Bookmarks timestamps are microseconds since the
// Windows epoch (1601-01-01).
base::Time FromChromeTime(int64_t microseconds) {
  if (microseconds <= 0) {
    return base::Time();
  }
  return base::Time::FromDeltaSinceWindowsEpoch(
      base::Microseconds(microseconds));
}

// Opens `db_name` under the profile via a private temp COPY, so the live source
// is never touched and a locked source cannot corrupt us. Returns false if the
// source is absent or the copy/open fails.
bool OpenCopied(const base::FilePath& profile_dir,
                const base::FilePath::CharType* db_name,
                base::ScopedTempDir* temp_dir,
                sql::Database* db) {
  const base::FilePath source = profile_dir.Append(db_name);
  if (!base::PathExists(source)) {
    return false;
  }
  if (!temp_dir->CreateUniqueTempDir()) {
    return false;
  }
  const base::FilePath copy = temp_dir->GetPath().Append(db_name);
  if (!base::CopyFile(source, copy)) {
    return false;
  }
  return db->Open(copy);
}

void CollectBookmarks(
    const base::DictValue& node,
    std::vector<std::u16string> path,
    bool in_toolbar,
    std::vector<user_data_importer::ImportedBookmarkEntry>* out) {
  const std::string* type = node.FindString("type");
  const std::string* name = node.FindString("name");
  if (!type || !name) {
    return;
  }
  const std::u16string title = base::UTF8ToUTF16(*name);
  if (*type == "folder") {
    const base::ListValue* children = node.FindList("children");
    if (!children || children->empty()) {
      // Preserve empty folders (roam-15 review finding 2): emit an is_folder
      // entry so ProfileWriter recreates it.
      user_data_importer::ImportedBookmarkEntry entry;
      entry.in_toolbar = in_toolbar;
      entry.is_folder = true;
      entry.path = path;
      entry.title = title;
      out->push_back(std::move(entry));
      return;
    }
    std::vector<std::u16string> child_path = path;
    child_path.push_back(title);
    for (const base::Value& child : *children) {
      if (child.is_dict()) {
        CollectBookmarks(child.GetDict(), child_path, in_toolbar, out);
      }
    }
    return;
  }
  if (*type != "url") {
    return;
  }
  const std::string* url = node.FindString("url");
  if (!url) {
    return;
  }
  user_data_importer::ImportedBookmarkEntry entry;
  entry.in_toolbar = in_toolbar;
  entry.is_folder = false;
  entry.url = GURL(*url);
  entry.path = path;
  entry.title = title;
  int64_t date_added = 0;
  if (const std::string* added = node.FindString("date_added")) {
    base::StringToInt64(*added, &date_added);
  }
  entry.creation_time = FromChromeTime(date_added);
  if (entry.url.is_valid()) {
    out->push_back(std::move(entry));
  }
}

void CollectRoot(const base::DictValue& root,
                 bool in_toolbar,
                 std::vector<user_data_importer::ImportedBookmarkEntry>* out) {
  const base::ListValue* children = root.FindList("children");
  if (!children) {
    return;
  }
  // Toolbar entries need a leading sentinel: ProfileWriter skips path[0] when
  // importing to an empty bar (so the real first folder survives). Non-toolbar
  // roots import under the destination folder, so no sentinel.
  std::vector<std::u16string> seed;
  if (in_toolbar) {
    const std::string* name = root.FindString("name");
    seed.push_back(name ? base::UTF8ToUTF16(*name) : u"Bookmarks bar");
  }
  for (const base::Value& child : *children) {
    if (child.is_dict()) {
      CollectBookmarks(child.GetDict(), seed, in_toolbar, out);
    }
  }
}

// roam-202: a candidate profile counts only if it holds at least one artifact
// an import stage can read — the full utility + browser carrier surface. Flat
// artifacts are checked with PathExists, the same shape rule
// EdgeImportAdapter::CarrierAvailable applies to Login Data / Cookies.
bool HasEdgeProfileData(const base::FilePath& dir) {
  static constexpr const base::FilePath::CharType* kFlatArtifacts[] = {
      FILE_PATH_LITERAL("Bookmarks"),  FILE_PATH_LITERAL("History"),
      FILE_PATH_LITERAL("Web Data"),   FILE_PATH_LITERAL("Login Data"),
      FILE_PATH_LITERAL("Cookies"),
  };
  for (const auto* artifact : kFlatArtifacts) {
    if (base::PathExists(dir.Append(artifact))) {
      return true;
    }
  }
  return base::DirectoryExists(dir.Append(FILE_PATH_LITERAL("Local Storage"))
                                   .Append(FILE_PATH_LITERAL("leveldb"))) ||
         base::DirectoryExists(dir.Append(FILE_PATH_LITERAL("IndexedDB")));
}

// A Local State profile reference is usable only as a single well-formed path
// component — the resolved directory must stay a direct child of the
// user-data dir. Local State is external, user-controlled input.
bool IsSafeProfileName(const std::string& name) {
  return !name.empty() && name != "." && name != ".." &&
         name.find('/') == std::string::npos &&
         name.find('\\') == std::string::npos;
}

// Size cap for the Local State read: profile metadata fits comfortably; an
// oversized file is treated as unreadable rather than parsed.
constexpr size_t kMaxLocalStateBytes = 1024 * 1024;

std::optional<base::DictValue> ReadLocalState(
    const base::FilePath& user_data_dir) {
  std::string contents;
  if (!base::ReadFileToStringWithMaxSize(
          user_data_dir.Append(FILE_PATH_LITERAL("Local State")), &contents,
          kMaxLocalStateBytes)) {
    return std::nullopt;
  }
  return base::JSONReader::ReadDict(contents, base::JSON_PARSE_RFC);
}

// Scan comparator: "Profile N" sorts by N ascending; equal suffixes tie-break
// by full-name lexicographic order; names without a parseable suffix sort
// after all numeric ones.
std::pair<int64_t, std::string> ProfileScanKey(const base::FilePath& dir) {
  const std::string name = dir.BaseName().AsUTF8Unsafe();
  int64_t number = std::numeric_limits<int64_t>::max();
  constexpr std::string_view kPrefix = "Profile ";
  if (base::StartsWith(name, kPrefix)) {
    int64_t parsed = 0;
    if (base::StringToInt64(name.substr(kPrefix.size()), &parsed)) {
      number = parsed;
    }
  }
  return {number, name};
}

}  // namespace

base::FilePath ResolveEdgeProfileDir(const base::FilePath& user_data_dir) {
  auto usable = [&user_data_dir](const std::string& name) -> base::FilePath {
    if (!IsSafeProfileName(name)) {
      return base::FilePath();
    }
    const base::FilePath dir =
        user_data_dir.Append(base::FilePath::FromUTF8Unsafe(name));
    if (base::DirectoryExists(dir) && HasEdgeProfileData(dir)) {
      return dir;
    }
    return base::FilePath();
  };

  // (1) Local State: profile.last_used, then (2) a sole info_cache entry.
  if (std::optional<base::DictValue> local_state =
          ReadLocalState(user_data_dir)) {
    if (const base::DictValue* profile = local_state->FindDict("profile")) {
      if (const std::string* last_used = profile->FindString("last_used")) {
        if (base::FilePath dir = usable(*last_used); !dir.empty()) {
          return dir;
        }
      }
      if (const base::DictValue* cache = profile->FindDict("info_cache");
          cache && cache->size() == 1) {
        if (base::FilePath dir = usable(cache->begin()->first); !dir.empty()) {
          return dir;
        }
      }
    }
  }

  // (3) The historical Default layout.
  const base::FilePath default_dir =
      user_data_dir.Append(FILE_PATH_LITERAL("Default"));
  if (base::DirectoryExists(default_dir) && HasEdgeProfileData(default_dir)) {
    return default_dir;
  }

  // (4) Deterministic "Profile *" scan.
  std::vector<base::FilePath> candidates;
  base::FileEnumerator scan(user_data_dir, /*recursive=*/false,
                            base::FileEnumerator::DIRECTORIES,
                            FILE_PATH_LITERAL("Profile *"));
  for (base::FilePath dir = scan.Next(); !dir.empty(); dir = scan.Next()) {
    candidates.push_back(dir);
  }
  std::sort(candidates.begin(), candidates.end(),
            [](const base::FilePath& a, const base::FilePath& b) {
              return ProfileScanKey(a) < ProfileScanKey(b);
            });
  for (const base::FilePath& dir : candidates) {
    if (HasEdgeProfileData(dir)) {
      return dir;
    }
  }
  return base::FilePath();
}

std::optional<user_data_importer::SourceProfile> DetectEdgeSourceProfile(
    const base::FilePath& app_data_root) {
  const base::FilePath profile_dir = ResolveEdgeProfileDir(
      app_data_root.Append(FILE_PATH_LITERAL("Microsoft Edge")));
  if (profile_dir.empty()) {
    return std::nullopt;
  }
  user_data_importer::SourceProfile edge;
  edge.importer_name = u"Microsoft Edge";
  edge.importer_type = user_data_importer::TYPE_EDGE_CHROMIUM;
  edge.source_path = profile_dir;
  edge.services_supported = user_data_importer::HISTORY |
                            user_data_importer::FAVORITES |
                            user_data_importer::SEARCH_ENGINES |
                            user_data_importer::AUTOFILL_FORM_DATA |
                            // roam-16: passwords/cookies now imported by the
                            // browser-side secret stage (not the utility
                            // importer).
                            user_data_importer::PASSWORDS |
                            user_data_importer::COOKIES;
  return edge;
}

EdgeProfileReader::EdgeProfileReader(const base::FilePath& profile_dir)
    : profile_dir_(profile_dir) {}

EdgeProfileReader::~EdgeProfileReader() = default;

std::vector<user_data_importer::ImporterURLRow> EdgeProfileReader::ReadHistory()
    const {
  std::vector<user_data_importer::ImporterURLRow> rows;
  base::ScopedTempDir temp_dir;
  sql::Database db(kReaderTag);
  if (!OpenCopied(profile_dir_, FILE_PATH_LITERAL("History"), &temp_dir, &db)) {
    return rows;
  }
  sql::Statement s(db.GetUniqueStatement(
      "SELECT url, title, visit_count, typed_count, last_visit_time "
      "FROM urls WHERE hidden = 0 ORDER BY last_visit_time DESC"));
  if (!s.is_valid()) {  // Schema drift (missing table/column): soft-fail.
    return rows;
  }
  while (s.Step()) {
    GURL url(s.ColumnStringView(0));
    if (!url.is_valid()) {
      continue;
    }
    // roam-203: Edge stores last_visit_time = 0 for urls rows with no recorded
    // visit (and FromChromeTime nulls every non-positive value, not just zero).
    // HistoryBackend::AddPagesWithDetails DCHECKs !last_visit.is_null() on every
    // row, and with DCHECKs off it drops them anyway via IsExpiredVisitTime: a
    // null base::Time's internal zero orders before the expiration cutoff.
    // Skipping here therefore matches release history-storage behaviour while
    // removing the dev-build abort. Do NOT synthesise a timestamp instead: for a
    // non-synced source the backend "makes up a visit" and calls AddVisit(),
    // fabricating a visit that never happened — the observed rows carry
    // visit_count = 0.
    const base::Time last_visit = FromChromeTime(s.ColumnInt64(4));
    if (last_visit.is_null()) {
      continue;
    }
    user_data_importer::ImporterURLRow row(url);
    row.title = s.ColumnString16(1);
    row.visit_count = s.ColumnInt(2);
    row.typed_count = s.ColumnInt(3);
    row.last_visit = last_visit;
    rows.push_back(std::move(row));
  }
  return rows;
}

std::vector<user_data_importer::ImportedBookmarkEntry>
EdgeProfileReader::ReadBookmarks() const {
  std::vector<user_data_importer::ImportedBookmarkEntry> entries;
  const base::FilePath source =
      profile_dir_.Append(FILE_PATH_LITERAL("Bookmarks"));
  std::string json;
  if (!base::PathExists(source) || !base::ReadFileToString(source, &json)) {
    return entries;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return entries;
  }
  const base::DictValue* roots = parsed->GetDict().FindDict("roots");
  if (!roots) {
    return entries;
  }
  // bookmark_bar -> in_toolbar; other/synced -> not.
  if (const base::DictValue* bar = roots->FindDict("bookmark_bar")) {
    CollectRoot(*bar, /*in_toolbar=*/true, &entries);
  }
  for (const char* key : {"other", "synced"}) {
    if (const base::DictValue* node = roots->FindDict(key)) {
      CollectRoot(*node, /*in_toolbar=*/false, &entries);
    }
  }
  return entries;
}

std::vector<user_data_importer::SearchEngineInfo>
EdgeProfileReader::ReadSearchEngines() const {
  std::vector<user_data_importer::SearchEngineInfo> engines;
  base::ScopedTempDir temp_dir;
  sql::Database db(kReaderTag);
  if (!OpenCopied(profile_dir_, FILE_PATH_LITERAL("Web Data"), &temp_dir,
                  &db)) {
    return engines;
  }
  sql::Statement s(
      db.GetUniqueStatement("SELECT short_name, keyword, url FROM keywords"));
  if (!s.is_valid()) {
    return engines;
  }
  while (s.Step()) {
    user_data_importer::SearchEngineInfo engine;
    engine.display_name = s.ColumnString16(0);
    engine.keyword = s.ColumnString16(1);
    engine.url = s.ColumnString16(2);
    if (!engine.url.empty()) {
      engines.push_back(std::move(engine));
    }
  }
  return engines;
}

std::vector<ImporterAutofillFormDataEntry> EdgeProfileReader::ReadAutofill()
    const {
  std::vector<ImporterAutofillFormDataEntry> entries;
  base::ScopedTempDir temp_dir;
  sql::Database db(kReaderTag);
  if (!OpenCopied(profile_dir_, FILE_PATH_LITERAL("Web Data"), &temp_dir,
                  &db)) {
    return entries;
  }
  sql::Statement s(db.GetUniqueStatement(
      "SELECT name, value, count, date_created, date_last_used "
      "FROM autofill"));
  if (!s.is_valid()) {
    return entries;
  }
  while (s.Step()) {
    ImporterAutofillFormDataEntry entry;
    entry.name = s.ColumnString16(0);
    entry.value = s.ColumnString16(1);
    entry.times_used = s.ColumnInt(2);
    // Web Data autofill dates are time_t (Unix seconds), unlike History.
    entry.first_used = base::Time::FromTimeT(s.ColumnInt64(3));
    entry.last_used = base::Time::FromTimeT(s.ColumnInt64(4));
    if (!entry.name.empty()) {
      entries.push_back(std::move(entry));
    }
  }
  return entries;
}

}  // namespace roamux

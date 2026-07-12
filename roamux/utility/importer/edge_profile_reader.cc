// SPDX-License-Identifier: Apache-2.0
#include "roamux/utility/importer/edge_profile_reader.h"

#include <utility>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
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

}  // namespace

std::optional<user_data_importer::SourceProfile> DetectEdgeSourceProfile(
    const base::FilePath& app_data_root) {
  const base::FilePath profile_dir =
      app_data_root.Append(FILE_PATH_LITERAL("Microsoft Edge"))
          .Append(FILE_PATH_LITERAL("Default"));
  if (!base::PathExists(profile_dir)) {
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
    user_data_importer::ImporterURLRow row(url);
    row.title = s.ColumnString16(1);
    row.visit_count = s.ColumnInt(2);
    row.typed_count = s.ColumnInt(3);
    row.last_visit = FromChromeTime(s.ColumnInt64(4));
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

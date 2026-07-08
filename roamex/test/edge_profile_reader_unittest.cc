// SPDX-License-Identifier: Apache-2.0
// roam-15 (I-3.1): the EdgeProfileReader reads a golden macOS Chromium-Edge
// 150.x profile at full record fidelity (the ≥99.9% bar) — every field of
// history/bookmarks/search/autofill — plus copy-to-temp / soft-fail posture.

#include "roamex/utility/importer/edge_profile_reader.h"

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace roamex {
namespace {

constexpr sql::Database::Tag kFixtureTag{"RoamexEdgeImporter"};

// Windows-epoch microseconds for a known instant.
int64_t ChromeMicros(base::Time t) {
  return t.ToDeltaSinceWindowsEpoch().InMicroseconds();
}

class EdgeProfileReaderTest : public testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(profile_.CreateUniqueTempDir()); }

  base::FilePath dir() const { return profile_.GetPath(); }

  void WriteHistory(base::Time last_visit) {
    sql::Database db(kFixtureTag);
    ASSERT_TRUE(db.Open(dir().Append(FILE_PATH_LITERAL("History"))));
    ASSERT_TRUE(db.Execute(
        "CREATE TABLE urls(id INTEGER PRIMARY KEY, url LONGVARCHAR, "
        "title LONGVARCHAR, visit_count INTEGER, typed_count INTEGER, "
        "last_visit_time INTEGER, hidden INTEGER DEFAULT 0)"));
    sql::Statement s(db.GetUniqueStatement(
        "INSERT INTO urls(url, title, visit_count, typed_count, "
        "last_visit_time, hidden) VALUES(?,?,?,?,?,0)"));
    s.BindString(0, "https://portal.example.test/dash");
    s.BindString16(1, u"Dashboard");
    s.BindInt(2, 7);
    s.BindInt(3, 3);
    s.BindInt64(4, ChromeMicros(last_visit));
    ASSERT_TRUE(s.Run());
  }

  void WriteWebData(base::Time autofill_first, base::Time autofill_last) {
    sql::Database db(kFixtureTag);
    ASSERT_TRUE(db.Open(dir().Append(FILE_PATH_LITERAL("Web Data"))));
    ASSERT_TRUE(db.Execute(
        "CREATE TABLE keywords(id INTEGER PRIMARY KEY, short_name VARCHAR, "
        "keyword VARCHAR, url VARCHAR)"));
    ASSERT_TRUE(db.Execute(
        "INSERT INTO keywords(short_name, keyword, url) "
        "VALUES('Example','ex','https://ex.test/s?q={searchTerms}')"));
    ASSERT_TRUE(db.Execute(
        "CREATE TABLE autofill(name VARCHAR, value VARCHAR, count INTEGER, "
        "date_created INTEGER, date_last_used INTEGER)"));
    sql::Statement s(db.GetUniqueStatement(
        "INSERT INTO autofill(name, value, count, date_created, "
        "date_last_used) VALUES(?,?,?,?,?)"));
    s.BindString16(0, u"email");
    s.BindString16(1, u"user@example.test");
    s.BindInt(2, 5);
    s.BindInt64(3, autofill_first.ToTimeT());
    s.BindInt64(4, autofill_last.ToTimeT());
    ASSERT_TRUE(s.Run());
  }

  void WriteBookmarks(base::Time added) {
    const std::string json = R"({"roots":{"bookmark_bar":{"type":"folder",
      "name":"Bookmarks bar","children":[{"type":"folder","name":"Work",
      "children":[{"type":"url","name":"Wiki","url":"https://wiki.test/",
      "date_added":")" + base::NumberToString(ChromeMicros(added)) +
                             R"("}]}]},
      "other":{"type":"folder","name":"Other","children":[]},
      "synced":{"type":"folder","name":"Mobile","children":[]}}})";
    ASSERT_TRUE(
        base::WriteFile(dir().Append(FILE_PATH_LITERAL("Bookmarks")), json));
  }

  base::ScopedTempDir profile_;
};

TEST_F(EdgeProfileReaderTest, HistoryFullFidelity) {
  const base::Time visit = base::Time::FromTimeT(1700000000);
  WriteHistory(visit);
  auto rows = EdgeProfileReader(dir()).ReadHistory();
  ASSERT_EQ(1u, rows.size());
  EXPECT_EQ("https://portal.example.test/dash", rows[0].url.spec());
  EXPECT_EQ(u"Dashboard", rows[0].title);
  EXPECT_EQ(7, rows[0].visit_count);
  EXPECT_EQ(3, rows[0].typed_count);
  EXPECT_EQ(visit, rows[0].last_visit);
}

TEST_F(EdgeProfileReaderTest, BookmarksFullFidelity) {
  const base::Time added = base::Time::FromTimeT(1699999999);
  WriteBookmarks(added);
  auto entries = EdgeProfileReader(dir()).ReadBookmarks();
  ASSERT_EQ(1u, entries.size());
  EXPECT_EQ(u"Wiki", entries[0].title);
  EXPECT_EQ("https://wiki.test/", entries[0].url.spec());
  EXPECT_TRUE(entries[0].in_toolbar);
  // A leading toolbar sentinel (bar name) precedes the real folder so
  // ProfileWriter's skip-first-element keeps "Work".
  ASSERT_EQ(2u, entries[0].path.size());
  EXPECT_EQ(u"Bookmarks bar", entries[0].path[0]);
  EXPECT_EQ(u"Work", entries[0].path[1]);
  EXPECT_EQ(added, entries[0].creation_time);
}

TEST_F(EdgeProfileReaderTest, SearchEnginesFullFidelity) {
  WriteWebData(base::Time::FromTimeT(1), base::Time::FromTimeT(2));
  auto engines = EdgeProfileReader(dir()).ReadSearchEngines();
  ASSERT_EQ(1u, engines.size());
  EXPECT_EQ(u"Example", engines[0].display_name);
  EXPECT_EQ(u"ex", engines[0].keyword);
  EXPECT_EQ(u"https://ex.test/s?q={searchTerms}", engines[0].url);
}

TEST_F(EdgeProfileReaderTest, AutofillFullFidelity) {
  const base::Time first = base::Time::FromTimeT(1699000000);
  const base::Time last = base::Time::FromTimeT(1700000000);
  WriteWebData(first, last);
  auto entries = EdgeProfileReader(dir()).ReadAutofill();
  ASSERT_EQ(1u, entries.size());
  EXPECT_EQ(u"email", entries[0].name);
  EXPECT_EQ(u"user@example.test", entries[0].value);
  EXPECT_EQ(5, entries[0].times_used);
  EXPECT_EQ(first, entries[0].first_used);
  EXPECT_EQ(last, entries[0].last_used);
}

TEST_F(EdgeProfileReaderTest, MissingFilesSoftFail) {
  EdgeProfileReader reader(dir());  // empty dir
  EXPECT_TRUE(reader.ReadHistory().empty());
  EXPECT_TRUE(reader.ReadBookmarks().empty());
  EXPECT_TRUE(reader.ReadSearchEngines().empty());
  EXPECT_TRUE(reader.ReadAutofill().empty());
}

TEST_F(EdgeProfileReaderTest, MalformedBookmarksSoftFail) {
  ASSERT_TRUE(base::WriteFile(dir().Append(FILE_PATH_LITERAL("Bookmarks")),
                              "{not valid json"));
  EXPECT_TRUE(EdgeProfileReader(dir()).ReadBookmarks().empty());
}

TEST_F(EdgeProfileReaderTest, CorruptedHistorySchemaSoftFails) {
  // A History DB missing the expected columns must not crash — empty result.
  sql::Database db(kFixtureTag);
  ASSERT_TRUE(db.Open(dir().Append(FILE_PATH_LITERAL("History"))));
  ASSERT_TRUE(db.Execute("CREATE TABLE urls(id INTEGER PRIMARY KEY)"));
  EXPECT_TRUE(EdgeProfileReader(dir()).ReadHistory().empty());
}

TEST_F(EdgeProfileReaderTest, CorruptedWebDataSchemaSoftFails) {
  sql::Database db(kFixtureTag);
  ASSERT_TRUE(db.Open(dir().Append(FILE_PATH_LITERAL("Web Data"))));
  ASSERT_TRUE(db.Execute("CREATE TABLE unrelated(x INTEGER)"));
  EdgeProfileReader reader(dir());
  EXPECT_TRUE(reader.ReadSearchEngines().empty());
  EXPECT_TRUE(reader.ReadAutofill().empty());
}

TEST_F(EdgeProfileReaderTest, EmptyFoldersPreserved) {
  const std::string json = R"({"roots":{"bookmark_bar":{"type":"folder",
    "name":"Bookmarks bar","children":[{"type":"folder","name":"Empty",
    "children":[]}]},"other":{"type":"folder","name":"Other","children":[]},
    "synced":{"type":"folder","name":"Mobile","children":[]}}})";
  ASSERT_TRUE(
      base::WriteFile(dir().Append(FILE_PATH_LITERAL("Bookmarks")), json));
  auto entries = EdgeProfileReader(dir()).ReadBookmarks();
  ASSERT_EQ(1u,
            entries.size());  // the empty "other"/"synced" roots emit nothing
  EXPECT_TRUE(entries[0].is_folder);
  EXPECT_EQ(u"Empty", entries[0].title);
  EXPECT_TRUE(entries[0].in_toolbar);
  ASSERT_EQ(1u, entries[0].path.size());
  EXPECT_EQ(u"Bookmarks bar", entries[0].path[0]);
}

TEST_F(EdgeProfileReaderTest, CopyToTempLeavesSourceUntouched) {
  WriteHistory(base::Time::FromTimeT(1700000000));
  const base::FilePath source = dir().Append(FILE_PATH_LITERAL("History"));
  base::File::Info before;
  ASSERT_TRUE(base::GetFileInfo(source, &before));
  EdgeProfileReader(dir()).ReadHistory();
  base::File::Info after;
  ASSERT_TRUE(base::GetFileInfo(source, &after));
  EXPECT_EQ(before.last_modified, after.last_modified);
  EXPECT_EQ(before.size, after.size);
}

}  // namespace
}  // namespace roamex

// --- Detection shape (roam-15, Step-5 findings 3/6): hermetic, pure. ---
namespace roamex {
namespace {

TEST(EdgeDetectionTest, PresentWithFourNonSecretBitsWhenDirExists) {
  base::ScopedTempDir app_data;
  ASSERT_TRUE(app_data.CreateUniqueTempDir());
  const base::FilePath edge_default =
      app_data.GetPath()
          .Append(FILE_PATH_LITERAL("Microsoft Edge"))
          .Append(FILE_PATH_LITERAL("Default"));
  ASSERT_TRUE(base::CreateDirectory(edge_default));

  std::optional<user_data_importer::SourceProfile> profile =
      DetectEdgeSourceProfile(app_data.GetPath());
  ASSERT_TRUE(profile.has_value());
  EXPECT_EQ(user_data_importer::TYPE_EDGE_CHROMIUM, profile->importer_type);
  EXPECT_EQ(edge_default, profile->source_path);
  const uint16_t expected = user_data_importer::HISTORY |
                            user_data_importer::FAVORITES |
                            user_data_importer::SEARCH_ENGINES |
                            user_data_importer::AUTOFILL_FORM_DATA;
  EXPECT_EQ(expected, profile->services_supported);
  // Never advertises secrets (roam-16).
  EXPECT_EQ(0, profile->services_supported & user_data_importer::PASSWORDS);
  EXPECT_EQ(0, profile->services_supported & user_data_importer::COOKIES);
}

TEST(EdgeDetectionTest, AbsentWhenDirMissing) {
  base::ScopedTempDir app_data;
  ASSERT_TRUE(app_data.CreateUniqueTempDir());
  EXPECT_FALSE(DetectEdgeSourceProfile(app_data.GetPath()).has_value());
}

}  // namespace
}  // namespace roamex

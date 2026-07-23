// SPDX-License-Identifier: Apache-2.0
// roam-15 (I-3.1): the EdgeProfileReader reads a golden macOS Chromium-Edge
// 150.x profile at full record fidelity (the ≥99.9% bar) — every field of
// history/bookmarks/search/autofill — plus copy-to-temp / soft-fail posture.

#include "roamux/utility/importer/edge_profile_reader.h"

#include "base/check.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace roamux {
namespace {

constexpr sql::Database::Tag kFixtureTag{"RoamuxEdgeImporter"};

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

  // Appends a row to the urls table WriteHistory() created, binding
  // last_visit_time as a raw integer so a fixture can reproduce exactly what
  // Edge stores for a url with no recorded visit (roam-203: the literal 0).
  void AppendHistoryRow(const std::string& url,
                        int64_t raw_last_visit_time,
                        int visit_count) {
    sql::Database db(kFixtureTag);
    ASSERT_TRUE(db.Open(dir().Append(FILE_PATH_LITERAL("History"))));
    sql::Statement s(db.GetUniqueStatement(
        "INSERT INTO urls(url, title, visit_count, typed_count, "
        "last_visit_time, hidden) VALUES(?,?,?,?,?,0)"));
    s.BindString(0, url);
    s.BindString16(1, u"Never visited");
    s.BindInt(2, visit_count);
    s.BindInt(3, 0);
    s.BindInt64(4, raw_last_visit_time);
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

// roam-203: Edge stores last_visit_time = 0 for urls rows with no recorded
// visit. HistoryBackend::AddPagesWithDetails DCHECKs !last_visit.is_null() on
// every row it is handed, so such a row aborts the browser right after an
// otherwise successful import. The reader must not emit them.
TEST_F(EdgeProfileReaderTest, HistorySkipsRowsWithoutALastVisit) {
  const base::Time visit = base::Time::FromTimeT(1700000000);
  WriteHistory(visit);
  AppendHistoryRow("https://never-visited.example.test/x",
                   /*raw_last_visit_time=*/0, /*visit_count=*/0);
  // FromChromeTime() nulls EVERY non-positive value, not just zero, so a
  // negative timestamp must be dropped too. Without this row a fix that
  // filtered the raw column against 0 would pass while still handing
  // AddPagesWithDetails a null-time row.
  AppendHistoryRow("https://negative-time.example.test/y",
                   /*raw_last_visit_time=*/-1, /*visit_count=*/0);

  auto rows = EdgeProfileReader(dir()).ReadHistory();

  // Both visit-less rows are dropped; the real one survives untouched.
  ASSERT_EQ(1u, rows.size());
  EXPECT_EQ("https://portal.example.test/dash", rows[0].url.spec());
  EXPECT_EQ(visit, rows[0].last_visit);
  // The invariant AddPagesWithDetails requires of every row we hand it.
  EXPECT_FALSE(rows[0].last_visit.is_null());
}

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
}  // namespace roamux

// --- Detection shape (roam-15, Step-5 findings 3/6): hermetic, pure. ---
namespace roamux {
namespace {

// roam-202 fixture: builds `<root>/Microsoft Edge/<profile…>` trees with
// arbitrary profile-dir names, per-artifact data, and an optional Local State.
class EdgeLayoutBuilder {
 public:
  explicit EdgeLayoutBuilder(const base::FilePath& app_data_root)
      : user_data_(
            app_data_root.Append(FILE_PATH_LITERAL("Microsoft Edge"))) {
    CHECK(base::CreateDirectory(user_data_));
  }

  base::FilePath AddProfile(const std::string& name) {
    const base::FilePath dir =
        user_data_.Append(base::FilePath::FromUTF8Unsafe(name));
    CHECK(base::CreateDirectory(dir));
    return dir;
  }

  // A profile with one recognizable artifact (Bookmarks).
  base::FilePath AddPopulatedProfile(const std::string& name) {
    const base::FilePath dir = AddProfile(name);
    CHECK(base::WriteFile(dir.Append(FILE_PATH_LITERAL("Bookmarks")), "{}"));
    return dir;
  }

  void WriteLocalState(const std::string& contents) {
    CHECK(base::WriteFile(user_data_.Append(FILE_PATH_LITERAL("Local State")),
                          contents));
  }

  const base::FilePath& user_data() const { return user_data_; }

 private:
  base::FilePath user_data_;
};

std::string LocalStateWithLastUsed(const std::string& name) {
  return R"({"profile":{"last_used":")" + name + R"("}})";
}

TEST(EdgeDetectionTest, PresentWithFourNonSecretBitsWhenDirExists) {
  base::ScopedTempDir app_data;
  ASSERT_TRUE(app_data.CreateUniqueTempDir());
  const base::FilePath edge_default =
      app_data.GetPath()
          .Append(FILE_PATH_LITERAL("Microsoft Edge"))
          .Append(FILE_PATH_LITERAL("Default"));
  ASSERT_TRUE(base::CreateDirectory(edge_default));
  // roam-202: detection now requires recognizable profile data, so the
  // back-compat Default profile carries one artifact.
  ASSERT_TRUE(
      base::WriteFile(edge_default.Append(FILE_PATH_LITERAL("Bookmarks")),
                      "{}"));

  std::optional<user_data_importer::SourceProfile> profile =
      DetectEdgeSourceProfile(app_data.GetPath());
  ASSERT_TRUE(profile.has_value());
  EXPECT_EQ(user_data_importer::TYPE_EDGE_CHROMIUM, profile->importer_type);
  EXPECT_EQ(edge_default, profile->source_path);
  const uint16_t expected = user_data_importer::HISTORY |
                            user_data_importer::FAVORITES |
                            user_data_importer::SEARCH_ENGINES |
                            user_data_importer::AUTOFILL_FORM_DATA |
                            user_data_importer::PASSWORDS |
                            user_data_importer::COOKIES;
  EXPECT_EQ(expected, profile->services_supported);
}

TEST(EdgeDetectionTest, AbsentWhenDirMissing) {
  base::ScopedTempDir app_data;
  ASSERT_TRUE(app_data.CreateUniqueTempDir());
  EXPECT_FALSE(DetectEdgeSourceProfile(app_data.GetPath()).has_value());
}

// roam-202 T1: the reported real-machine layout — a populated "Profile 1",
// Local State naming it, and no "Default" at all.
TEST(EdgeDetectionTest, LastUsedProfileOneDetected) {
  base::ScopedTempDir app_data;
  ASSERT_TRUE(app_data.CreateUniqueTempDir());
  EdgeLayoutBuilder edge(app_data.GetPath());
  const base::FilePath profile1 = edge.AddPopulatedProfile("Profile 1");
  edge.WriteLocalState(LocalStateWithLastUsed("Profile 1"));

  std::optional<user_data_importer::SourceProfile> profile =
      DetectEdgeSourceProfile(app_data.GetPath());
  ASSERT_TRUE(profile.has_value());
  EXPECT_EQ(profile1, profile->source_path);
}

// roam-202 T2: last_used is authoritative, not directory order.
TEST(EdgeDetectionTest, LastUsedBeatsAlphabetical) {
  base::ScopedTempDir app_data;
  ASSERT_TRUE(app_data.CreateUniqueTempDir());
  EdgeLayoutBuilder edge(app_data.GetPath());
  edge.AddPopulatedProfile("Profile 1");
  const base::FilePath profile2 = edge.AddPopulatedProfile("Profile 2");
  edge.WriteLocalState(LocalStateWithLastUsed("Profile 2"));

  std::optional<user_data_importer::SourceProfile> profile =
      DetectEdgeSourceProfile(app_data.GetPath());
  ASSERT_TRUE(profile.has_value());
  EXPECT_EQ(profile2, profile->source_path);
}

// roam-202 T3: unreadable Local State falls back to a populated Default.
TEST(EdgeDetectionTest, MalformedLocalStateFallsBack) {
  base::ScopedTempDir app_data;
  ASSERT_TRUE(app_data.CreateUniqueTempDir());
  EdgeLayoutBuilder edge(app_data.GetPath());
  const base::FilePath def = edge.AddPopulatedProfile("Default");
  edge.WriteLocalState("not json at all {{{");

  std::optional<user_data_importer::SourceProfile> profile =
      DetectEdgeSourceProfile(app_data.GetPath());
  ASSERT_TRUE(profile.has_value());
  EXPECT_EQ(def, profile->source_path);
}

// roam-202 T4: a Local State beyond the size cap is treated as unreadable.
TEST(EdgeDetectionTest, OversizedLocalStateIgnored) {
  base::ScopedTempDir app_data;
  ASSERT_TRUE(app_data.CreateUniqueTempDir());
  EdgeLayoutBuilder edge(app_data.GetPath());
  const base::FilePath def = edge.AddPopulatedProfile("Default");
  edge.WriteLocalState(LocalStateWithLastUsed("Default") +
                       std::string(2 * 1024 * 1024, ' '));

  std::optional<user_data_importer::SourceProfile> profile =
      DetectEdgeSourceProfile(app_data.GetPath());
  ASSERT_TRUE(profile.has_value());
  EXPECT_EQ(def, profile->source_path);
}

// roam-202 T5: a traversal-shaped last_used must not escape the user-data
// root, even when a populated directory exists at exactly that relative path.
TEST(EdgeDetectionTest, TraversalDecoyNotFollowed) {
  base::ScopedTempDir app_data;
  ASSERT_TRUE(app_data.CreateUniqueTempDir());
  EdgeLayoutBuilder edge(app_data.GetPath());
  // The decoy: `<user_data>/../evil` == `<app_data>/evil`, populated.
  const base::FilePath decoy = app_data.GetPath().Append(
      FILE_PATH_LITERAL("evil"));
  ASSERT_TRUE(base::CreateDirectory(decoy));
  ASSERT_TRUE(
      base::WriteFile(decoy.Append(FILE_PATH_LITERAL("Bookmarks")), "{}"));
  const base::FilePath profile1 = edge.AddPopulatedProfile("Profile 1");
  edge.WriteLocalState(LocalStateWithLastUsed("../evil"));

  std::optional<user_data_importer::SourceProfile> profile =
      DetectEdgeSourceProfile(app_data.GetPath());
  ASSERT_TRUE(profile.has_value());
  EXPECT_EQ(profile1, profile->source_path);
}

// roam-202 T6: a bare Default directory with no data is not an install.
TEST(EdgeDetectionTest, BareDefaultNotDetected) {
  base::ScopedTempDir app_data;
  ASSERT_TRUE(app_data.CreateUniqueTempDir());
  EdgeLayoutBuilder edge(app_data.GetPath());
  edge.AddProfile("Default");

  EXPECT_FALSE(DetectEdgeSourceProfile(app_data.GetPath()).has_value());
}

// roam-202 T7: without Local State the scan picks the lowest profile number,
// numerically — Profile 2 before Profile 10.
TEST(EdgeDetectionTest, ScanOrderNumericAscending) {
  base::ScopedTempDir app_data;
  ASSERT_TRUE(app_data.CreateUniqueTempDir());
  EdgeLayoutBuilder edge(app_data.GetPath());
  edge.AddPopulatedProfile("Profile 10");
  const base::FilePath profile2 = edge.AddPopulatedProfile("Profile 2");

  std::optional<user_data_importer::SourceProfile> profile =
      DetectEdgeSourceProfile(app_data.GetPath());
  ASSERT_TRUE(profile.has_value());
  EXPECT_EQ(profile2, profile->source_path);
}

// roam-202 T8: equal numeric suffixes tie-break by full-name lexicographic
// order — "Profile 02" before "Profile 2".
TEST(EdgeDetectionTest, ScanTieBreakLexicalOnEqualSuffix) {
  base::ScopedTempDir app_data;
  ASSERT_TRUE(app_data.CreateUniqueTempDir());
  EdgeLayoutBuilder edge(app_data.GetPath());
  edge.AddPopulatedProfile("Profile 2");
  const base::FilePath profile02 = edge.AddPopulatedProfile("Profile 02");

  std::optional<user_data_importer::SourceProfile> profile =
      DetectEdgeSourceProfile(app_data.GetPath());
  ASSERT_TRUE(profile.has_value());
  EXPECT_EQ(profile02, profile->source_path);
}

// roam-202 T9: no last_used, exactly one info_cache entry — that entry wins.
TEST(EdgeDetectionTest, InfoCacheSingleEntryFallback) {
  base::ScopedTempDir app_data;
  ASSERT_TRUE(app_data.CreateUniqueTempDir());
  EdgeLayoutBuilder edge(app_data.GetPath());
  const base::FilePath profile1 = edge.AddPopulatedProfile("Profile 1");
  edge.WriteLocalState(
      R"({"profile":{"info_cache":{"Profile 1":{"name":"Personal"}}}})");

  std::optional<user_data_importer::SourceProfile> profile =
      DetectEdgeSourceProfile(app_data.GetPath());
  ASSERT_TRUE(profile.has_value());
  EXPECT_EQ(profile1, profile->source_path);
}

// roam-202 T10: a last_used naming a missing directory falls through to the
// info_cache entry.
TEST(EdgeDetectionTest, DanglingLastUsedFallsThrough) {
  base::ScopedTempDir app_data;
  ASSERT_TRUE(app_data.CreateUniqueTempDir());
  EdgeLayoutBuilder edge(app_data.GetPath());
  const base::FilePath profile1 = edge.AddPopulatedProfile("Profile 1");
  edge.WriteLocalState(
      R"({"profile":{"last_used":"Profile 7",)"
      R"("info_cache":{"Profile 1":{"name":"Personal"}}}})");

  std::optional<user_data_importer::SourceProfile> profile =
      DetectEdgeSourceProfile(app_data.GetPath());
  ASSERT_TRUE(profile.has_value());
  EXPECT_EQ(profile1, profile->source_path);
}

// roam-202 T11: a non-string last_used is ignored, not crashed on.
TEST(EdgeDetectionTest, WrongTypeLastUsedFallsThrough) {
  base::ScopedTempDir app_data;
  ASSERT_TRUE(app_data.CreateUniqueTempDir());
  EdgeLayoutBuilder edge(app_data.GetPath());
  const base::FilePath def = edge.AddPopulatedProfile("Default");
  edge.WriteLocalState(R"({"profile":{"last_used":42}})");

  std::optional<user_data_importer::SourceProfile> profile =
      DetectEdgeSourceProfile(app_data.GetPath());
  ASSERT_TRUE(profile.has_value());
  EXPECT_EQ(def, profile->source_path);
}

// roam-202 T12: every carrier artifact — utility and browser side — counts as
// recognizable profile data on its own.
TEST(EdgeDetectionTest, EachArtifactAloneIsRecognized) {
  struct Artifact {
    const base::FilePath::CharType* first;
    const base::FilePath::CharType* second;  // nullptr = single component
    bool is_dir;
  };
  constexpr Artifact kArtifacts[] = {
      {FILE_PATH_LITERAL("Bookmarks"), nullptr, false},
      {FILE_PATH_LITERAL("History"), nullptr, false},
      {FILE_PATH_LITERAL("Web Data"), nullptr, false},
      {FILE_PATH_LITERAL("Login Data"), nullptr, false},
      {FILE_PATH_LITERAL("Cookies"), nullptr, false},
      {FILE_PATH_LITERAL("Local Storage"), FILE_PATH_LITERAL("leveldb"), true},
      {FILE_PATH_LITERAL("IndexedDB"), nullptr, true},
  };
  for (const Artifact& artifact : kArtifacts) {
    SCOPED_TRACE(base::FilePath(artifact.first).AsUTF8Unsafe());
    base::ScopedTempDir app_data;
    ASSERT_TRUE(app_data.CreateUniqueTempDir());
    EdgeLayoutBuilder edge(app_data.GetPath());
    const base::FilePath profile1 = edge.AddProfile("Profile 1");
    base::FilePath target = profile1.Append(artifact.first);
    if (artifact.second) {
      target = target.Append(artifact.second);
    }
    if (artifact.is_dir) {
      ASSERT_TRUE(base::CreateDirectory(target));
    } else {
      ASSERT_TRUE(base::WriteFile(target, "x"));
    }
    edge.WriteLocalState(LocalStateWithLastUsed("Profile 1"));

    std::optional<user_data_importer::SourceProfile> profile =
        DetectEdgeSourceProfile(app_data.GetPath());
    ASSERT_TRUE(profile.has_value());
    EXPECT_EQ(profile1, profile->source_path);
  }
}

// roam-202 T13: flat artifacts are checked with PathExists (the adapter's
// CarrierAvailable rule) — a directory-shaped "Login Data" counts.
TEST(EdgeDetectionTest, DirectoryShapedFlatArtifactCounts) {
  base::ScopedTempDir app_data;
  ASSERT_TRUE(app_data.CreateUniqueTempDir());
  EdgeLayoutBuilder edge(app_data.GetPath());
  const base::FilePath profile1 = edge.AddProfile("Profile 1");
  ASSERT_TRUE(base::CreateDirectory(
      profile1.Append(FILE_PATH_LITERAL("Login Data"))));
  edge.WriteLocalState(LocalStateWithLastUsed("Profile 1"));

  std::optional<user_data_importer::SourceProfile> profile =
      DetectEdgeSourceProfile(app_data.GetPath());
  ASSERT_TRUE(profile.has_value());
  EXPECT_EQ(profile1, profile->source_path);
}

}  // namespace
}  // namespace roamux

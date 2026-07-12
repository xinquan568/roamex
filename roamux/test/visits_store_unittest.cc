// SPDX-License-Identifier: Apache-2.0
// roam-21 (I-4.1): the writable visits store — append-only, the 11-entry
// scenario, cap-999 FIFO (oldest-out), on-disk-vs-in-memory, close/reopen
// persistence, and raze-on-corrupt.

#include "roamux/browser/tab_visit/visits_store.h"

#include <memory>
#include <string>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/stringprintf.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "sql/database.h"
#include "sql/meta_table.h"
#include "sql/statement.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace roamux::tab_visit {
namespace {

class VisitsStoreTest : public testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(dir_.CreateUniqueTempDir()); }
  base::FilePath DbPath() {
    return dir_.GetPath().Append(FILE_PATH_LITERAL("RoamuxTabVisits"));
  }

  base::test::TaskEnvironment task_environment_;
  base::ScopedTempDir dir_;
};

TEST_F(VisitsStoreTest, AppendOnlyElevenEntryScenarioInOrder) {
  VisitsStore store;
  ASSERT_TRUE(store.OpenInMemory());
  const base::Time t0 = base::Time::Now();
  for (int i = 0; i < 11; ++i) {
    store.Append("u", base::StringPrintf("https://example.test/%d", i),
                 t0 + base::Seconds(i));
  }
  std::vector<VisitRow> rows = store.ReadAll();
  ASSERT_EQ(11u, rows.size());
  for (int i = 0; i < 11; ++i) {
    EXPECT_EQ(base::StringPrintf("https://example.test/%d", i), rows[i].url);
    EXPECT_EQ(t0 + base::Seconds(i), rows[i].visited_at);
  }
}

TEST_F(VisitsStoreTest, Cap999FifoEvictsOldest) {
  VisitsStore store;
  ASSERT_TRUE(store.OpenInMemory());
  const base::Time t0 = base::Time::Now();
  for (int i = 0; i < 1000; ++i) {
    store.Append("u", base::StringPrintf("https://example.test/%d", i),
                 t0 + base::Milliseconds(i));
  }
  EXPECT_EQ(VisitsStore::kMaxVisits, store.RowCount());
  std::vector<VisitRow> rows = store.ReadAll();
  ASSERT_EQ(VisitsStore::kMaxVisits, rows.size());
  // Oldest (index 0) evicted; newest (index 999) retained.
  EXPECT_EQ("https://example.test/1", rows.front().url);
  EXPECT_EQ("https://example.test/999", rows.back().url);
}

TEST_F(VisitsStoreTest, OnDiskCreatesFileInMemoryDoesNot) {
  {
    VisitsStore file_store;
    ASSERT_TRUE(file_store.Open(DbPath()));
    file_store.Append("u", "https://a.test/", base::Time::Now());
  }
  EXPECT_TRUE(base::PathExists(DbPath()));

  const base::FilePath other =
      dir_.GetPath().Append(FILE_PATH_LITERAL("mem-marker"));
  {
    VisitsStore mem_store;
    ASSERT_TRUE(mem_store.OpenInMemory());
    mem_store.Append("u", "https://b.test/", base::Time::Now());
    EXPECT_EQ(1u, mem_store.ReadAll().size());
  }
  // An in-memory store never creates any file (no path was ever supplied).
  EXPECT_FALSE(base::PathExists(other));
}

TEST_F(VisitsStoreTest, PersistsAcrossCloseAndReopen) {
  const base::Time t = base::Time::Now();
  {
    VisitsStore store;
    ASSERT_TRUE(store.Open(DbPath()));
    store.Append("u", "https://persist.test/x", t);
  }
  VisitsStore reopened;
  ASSERT_TRUE(reopened.Open(DbPath()));
  std::vector<VisitRow> rows = reopened.ReadAll();
  ASSERT_EQ(1u, rows.size());
  EXPECT_EQ("https://persist.test/x", rows[0].url);
  EXPECT_EQ(t, rows[0].visited_at);
}

TEST_F(VisitsStoreTest, CorruptDatabaseIsRazedAndReset) {
  ASSERT_TRUE(base::WriteFile(DbPath(), "this is not a sqlite database"));
  VisitsStore store;
  ASSERT_TRUE(store.Open(DbPath()));
  EXPECT_EQ(0u, store.RowCount());
  store.Append("u", "https://after-raze.test/", base::Time::Now());
  EXPECT_EQ(1u, store.RowCount());
}

// --- roam-22: the tab_state sidecar ---

TEST_F(VisitsStoreTest, TabStateUpsertReplaceAndRead) {
  VisitsStore store;
  ASSERT_TRUE(store.OpenInMemory());
  store.UpsertTabState({"key-1", /*closed=*/false, /*window_id=*/7, "u1"});
  std::optional<TabStateRow> row = store.GetTabState("key-1");
  ASSERT_TRUE(row);
  EXPECT_FALSE(row->closed);
  EXPECT_EQ(7, row->window_id);
  EXPECT_EQ("u1", row->last_known_url);

  // Same key ⇒ replace (mutable, not append-only).
  store.UpsertTabState({"key-1", /*closed=*/true, /*window_id=*/9, "u2"});
  row = store.GetTabState("key-1");
  ASSERT_TRUE(row);
  EXPECT_TRUE(row->closed);
  EXPECT_EQ(9, row->window_id);
  EXPECT_EQ("u2", row->last_known_url);
  EXPECT_EQ(1u, store.ReadTabStates().size());  // one row, replaced in place
  EXPECT_FALSE(store.GetTabState("absent"));
}

TEST_F(VisitsStoreTest, TabStatePersistsAcrossReopen) {
  {
    VisitsStore store;
    ASSERT_TRUE(store.Open(DbPath()));
    store.UpsertTabState({"k", /*closed=*/true, /*window_id=*/3, "https://x/"});
  }
  VisitsStore reopened;
  ASSERT_TRUE(reopened.Open(DbPath()));
  std::optional<TabStateRow> row = reopened.GetTabState("k");
  ASSERT_TRUE(row);
  EXPECT_TRUE(row->closed);
  EXPECT_EQ("https://x/", row->last_known_url);
}

TEST_F(VisitsStoreTest, MigratesRealV1JournalPreservingVisits) {
  // Build a genuine roam-21 v1 journal directly: MetaTable v1, only the
  // `visits` table, one row — exactly what a pre-roam-22 profile has on disk.
  {
    sql::Database db(sql::Database::Tag("RoamuxSettledVisitJournal"));
    ASSERT_TRUE(db.Open(DbPath()));
    sql::MetaTable meta;
    ASSERT_TRUE(meta.Init(&db, /*version=*/1, /*compatible_version=*/1));
    ASSERT_TRUE(
        db.Execute("CREATE TABLE visits(id INTEGER PRIMARY KEY AUTOINCREMENT,"
                   "url TEXT NOT NULL,visited_at INTEGER NOT NULL)"));
    sql::Statement s(db.GetUniqueStatement(
        "INSERT INTO visits(url,visited_at) VALUES(?,?)"));
    s.BindString(0, "https://legacy.test/");
    s.BindInt64(1, 123456);
    ASSERT_TRUE(s.Run());
  }

  // Open through the v2 store: the migration runs.
  {
    VisitsStore store;
    ASSERT_TRUE(store.Open(DbPath()));
    // The pre-existing visit survives.
    std::vector<VisitRow> visits = store.ReadAll();
    ASSERT_EQ(1u, visits.size());
    EXPECT_EQ("https://legacy.test/", visits[0].url);
    // The new tab_state table now works.
    store.UpsertTabState({"k", true, 1, "https://y/"});
    EXPECT_TRUE(store.GetTabState("k"));
  }

  // The stored schema version has advanced to the current version (v1 -> v3).
  sql::Database db(sql::Database::Tag("RoamuxSettledVisitJournal"));
  ASSERT_TRUE(db.Open(DbPath()));
  sql::MetaTable meta;
  ASSERT_TRUE(meta.Init(&db, /*version=*/3, /*compatible_version=*/1));
  EXPECT_EQ(3, meta.GetVersionNumber());
}

// roam-26: a genuine v2 journal (visits + tab_state, no tab_uid column)
// migrates to v3 — the tab_uid column is added, existing visits are preserved,
// and the new column defaults to ''.
TEST_F(VisitsStoreTest, MigratesRealV2JournalAddingUidColumn) {
  {
    sql::Database db(sql::Database::Tag("RoamuxSettledVisitJournal"));
    ASSERT_TRUE(db.Open(DbPath()));
    sql::MetaTable meta;
    ASSERT_TRUE(meta.Init(&db, /*version=*/2, /*compatible_version=*/1));
    ASSERT_TRUE(
        db.Execute("CREATE TABLE visits(id INTEGER PRIMARY KEY AUTOINCREMENT,"
                   "url TEXT NOT NULL,visited_at INTEGER NOT NULL)"));
    ASSERT_TRUE(
        db.Execute("CREATE TABLE tab_state(restore_key TEXT PRIMARY KEY,"
                   "closed INTEGER NOT NULL,window_id INTEGER NOT NULL,"
                   "last_known_url TEXT NOT NULL)"));
    sql::Statement s(db.GetUniqueStatement(
        "INSERT INTO visits(url,visited_at) VALUES(?,?)"));
    s.BindString(0, "https://v2.test/");
    s.BindInt64(1, 222222);
    ASSERT_TRUE(s.Run());
  }

  {
    VisitsStore store;
    ASSERT_TRUE(store.Open(DbPath()));
    std::vector<VisitRow> visits = store.ReadAll();
    ASSERT_EQ(1u, visits.size());
    EXPECT_EQ("https://v2.test/", visits[0].url);
    EXPECT_EQ("", visits[0].tab_uid);  // Legacy row: default ''.
    // A fresh uid-keyed append round-trips.
    store.Append("uid-new", "https://v3.test/", base::Time::Now());
    visits = store.ReadAll();
    ASSERT_EQ(2u, visits.size());
    EXPECT_EQ("uid-new", visits[1].tab_uid);
  }

  sql::Database db(sql::Database::Tag("RoamuxSettledVisitJournal"));
  ASSERT_TRUE(db.Open(DbPath()));
  sql::MetaTable meta;
  ASSERT_TRUE(meta.Init(&db, /*version=*/3, /*compatible_version=*/1));
  EXPECT_EQ(3, meta.GetVersionNumber());
}

// roam-26: a PARTIAL migration (a crash added the tab_uid column but the
// version is still 2) must complete cleanly — the column-existence guard skips
// the non-idempotent ALTER, the version bumps to 3, and no raze/data loss
// occurs.
TEST_F(VisitsStoreTest, PartialV3MigrationDoesNotRaze) {
  {
    sql::Database db(sql::Database::Tag("RoamuxSettledVisitJournal"));
    ASSERT_TRUE(db.Open(DbPath()));
    sql::MetaTable meta;
    ASSERT_TRUE(meta.Init(&db, /*version=*/2, /*compatible_version=*/1));
    // The tab_uid column is ALREADY present, but the version is still 2.
    ASSERT_TRUE(
        db.Execute("CREATE TABLE visits(id INTEGER PRIMARY KEY AUTOINCREMENT,"
                   "tab_uid TEXT NOT NULL DEFAULT '',url TEXT NOT NULL,"
                   "visited_at INTEGER NOT NULL)"));
    sql::Statement s(db.GetUniqueStatement(
        "INSERT INTO visits(tab_uid,url,visited_at) VALUES(?,?,?)"));
    s.BindString(0, "uid-partial");
    s.BindString(1, "https://partial.test/");
    s.BindInt64(2, 333333);
    ASSERT_TRUE(s.Run());
  }

  {
    VisitsStore store;
    ASSERT_TRUE(store.Open(DbPath()));  // Must NOT raze on duplicate-column.
    std::vector<VisitRow> visits = store.ReadAll();
    ASSERT_EQ(1u, visits.size());  // Row preserved.
    EXPECT_EQ("https://partial.test/", visits[0].url);
    EXPECT_EQ("uid-partial", visits[0].tab_uid);
  }

  sql::Database db(sql::Database::Tag("RoamuxSettledVisitJournal"));
  ASSERT_TRUE(db.Open(DbPath()));
  sql::MetaTable meta;
  ASSERT_TRUE(meta.Init(&db, /*version=*/3, /*compatible_version=*/1));
  EXPECT_EQ(3, meta.GetVersionNumber());
}

// roam-28 (I-4.8): all-time clear (null begin + max end) truncates every visit.
TEST_F(VisitsStoreTest, ClearAllTimeTruncatesVisits) {
  VisitsStore store;
  ASSERT_TRUE(store.OpenInMemory());
  const base::Time t = base::Time::Now();
  store.Append("a", "https://a.test/", t - base::Days(2));
  store.Append("b", "https://b.test/", t);
  ASSERT_EQ(2u, store.RowCount());

  store.ClearForBrowsingDataRemoval(base::Time(), base::Time::Max());
  EXPECT_EQ(0u, store.RowCount());
}

// Time-ranged clear is begin-INCLUSIVE, end-EXCLUSIVE: a row at exactly `end`
// SURVIVES; a row at exactly `begin` is removed.
TEST_F(VisitsStoreTest, ClearRangeIsBeginInclusiveEndExclusive) {
  VisitsStore store;
  ASSERT_TRUE(store.OpenInMemory());
  const base::Time base = base::Time::Now();
  const base::Time begin = base;
  const base::Time end = base + base::Hours(2);
  store.Append("before", "https://before.test/", begin - base::Minutes(1));
  store.Append("at_begin", "https://at-begin.test/", begin);  // removed (>=)
  store.Append("mid", "https://mid.test/", begin + base::Hours(1));  // removed
  store.Append("at_end", "https://at-end.test/", end);  // SURVIVES (<)
  store.Append("after", "https://after.test/", end + base::Minutes(1));

  store.ClearForBrowsingDataRemoval(begin, end);

  std::vector<VisitRow> rows = store.ReadAll();
  ASSERT_EQ(3u, rows.size());
  EXPECT_EQ("before", rows[0].tab_uid);
  EXPECT_EQ("at_end", rows[1].tab_uid);  // exactly `end` was kept.
  EXPECT_EQ("after", rows[2].tab_uid);
}

// One-sided bound: null begin + finite end removes everything strictly before
// end (no lower bound).
TEST_F(VisitsStoreTest, ClearOpenBeginFiniteEnd) {
  VisitsStore store;
  ASSERT_TRUE(store.OpenInMemory());
  const base::Time base = base::Time::Now();
  store.Append("old", "https://old.test/", base - base::Days(30));
  store.Append("recent", "https://recent.test/", base + base::Days(1));

  store.ClearForBrowsingDataRemoval(base::Time(), base);  // (-inf, base)
  std::vector<VisitRow> rows = store.ReadAll();
  ASSERT_EQ(1u, rows.size());
  EXPECT_EQ("recent", rows[0].tab_uid);  // only the one >= base survived.
}

// One-sided bound: finite begin + max end removes everything at/after begin (no
// upper bound) WITHOUT serializing base::Time::Max().
TEST_F(VisitsStoreTest, ClearFiniteBeginOpenEnd) {
  VisitsStore store;
  ASSERT_TRUE(store.OpenInMemory());
  const base::Time base = base::Time::Now();
  store.Append("old", "https://old.test/", base - base::Days(1));
  store.Append("at", "https://at.test/", base);
  store.Append("new", "https://new.test/", base + base::Days(1));

  store.ClearForBrowsingDataRemoval(base, base::Time::Max());  // [base, +inf)
  std::vector<VisitRow> rows = store.ReadAll();
  ASSERT_EQ(1u, rows.size());
  EXPECT_EQ("old", rows[0].tab_uid);  // only the one < base survived.
}

// GC removes ONLY closed tab_state rows whose uid has no surviving visit; live
// rows and closed rows with a surviving visit are kept.
TEST_F(VisitsStoreTest, ClearGCsOnlyOrphanedClosedTabStates) {
  VisitsStore store;
  ASSERT_TRUE(store.OpenInMemory());
  const base::Time base = base::Time::Now();
  // A closed tab whose only visit is in-range (will be orphaned by the clear).
  store.Append("orphan", "https://orphan.test/", base);
  store.UpsertTabState({"orphan", /*closed=*/true, 1, "https://orphan.test/"});
  // A closed tab whose visit is OUT of range (survives -> not orphaned).
  store.Append("kept-closed", "https://kept.test/", base - base::Days(10));
  store.UpsertTabState(
      {"kept-closed", /*closed=*/true, 2, "https://kept.test/"});
  // A LIVE tab (closed=0) — must NEVER be GC'd, even with no visit.
  store.UpsertTabState({"live", /*closed=*/false, 3, "https://live.test/"});
  ASSERT_EQ(3u, store.ReadTabStates().size());

  // Clear [base, +inf): removes the "orphan" visit, keeps "kept-closed".
  store.ClearForBrowsingDataRemoval(base, base::Time::Max());

  std::vector<TabStateRow> states = store.ReadTabStates();
  ASSERT_EQ(2u, states.size());
  bool has_live = false, has_kept = false, has_orphan = false;
  for (const TabStateRow& r : states) {
    has_live |= r.restore_key == "live";
    has_kept |= r.restore_key == "kept-closed";
    has_orphan |= r.restore_key == "orphan";
  }
  EXPECT_TRUE(has_live);     // live row never touched (§6.9).
  EXPECT_TRUE(has_kept);     // closed row with a surviving visit kept.
  EXPECT_FALSE(has_orphan);  // closed + orphaned row GC'd.
}

}  // namespace
}  // namespace roamux::tab_visit

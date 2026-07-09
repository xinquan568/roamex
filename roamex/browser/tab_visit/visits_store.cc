// SPDX-License-Identifier: Apache-2.0
#include "roamex/browser/tab_visit/visits_store.h"

#include <utility>

#include "base/files/file_util.h"
#include "sql/meta_table.h"
#include "sql/statement.h"
#include "sql/transaction.h"

namespace roamex::tab_visit {

namespace {

// SQLite UMA tag — registered as a variant in
// tools/metrics/histograms/metadata/sql/histograms.xml (patch 0015).
constexpr char kDatabaseTag[] = "RoamexSettledVisitJournal";

// v1 (roam-21): the `visits` table. v2 (roam-22): adds the `tab_state` sidecar.
// Compatible version stays 1 — a v1 build can still open a v2 DB (it just
// ignores the `tab_state` table).
constexpr int kCurrentVersion = 2;
constexpr int kCompatibleVersion = 1;

int64_t ToStorage(base::Time t) {
  return t.ToDeltaSinceWindowsEpoch().InMicroseconds();
}

base::Time FromStorage(int64_t v) {
  return base::Time::FromDeltaSinceWindowsEpoch(base::Microseconds(v));
}

}  // namespace

VisitsStore::VisitsStore() : db_(sql::Database::Tag(kDatabaseTag)) {}

VisitsStore::~VisitsStore() = default;

bool VisitsStore::Open(const base::FilePath& db_path) {
  if (db_.Open(db_path) && InitSchema()) {
    return true;
  }
  // The on-disk journal is corrupt/unusable/incompatible. It is a best-effort,
  // rebuildable record — delete it and recreate a fresh one rather than failing
  // the browser.
  db_.Close();
  base::DeleteFile(db_path);
  return db_.Open(db_path) && InitSchema();
}

bool VisitsStore::OpenInMemory() {
  return db_.OpenInMemory() && InitSchema();
}

bool VisitsStore::InitSchema() {
  static constexpr char kCreateVisits[] =
      "CREATE TABLE IF NOT EXISTS visits("
      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "url TEXT NOT NULL,"
      "visited_at INTEGER NOT NULL)";
  // The persisted tab-state sidecar (roam-22). Keyed by the durable
  // restore_key; mutable (upsert). It deliberately has NO
  // liveSessionId/liveTabs column — those are volatile, in-memory-only, and
  // must never be persisted.
  static constexpr char kCreateTabState[] =
      "CREATE TABLE IF NOT EXISTS tab_state("
      "restore_key TEXT PRIMARY KEY,"
      "closed INTEGER NOT NULL,"
      "window_id INTEGER NOT NULL,"
      "last_known_url TEXT NOT NULL)";

  sql::MetaTable meta;
  // A future/incompatible schema is treated as unusable so the caller resets
  // it.
  if (!meta.Init(&db_, kCurrentVersion, kCompatibleVersion) ||
      meta.GetCompatibleVersionNumber() > kCurrentVersion ||
      !db_.Execute(kCreateVisits) || !db_.Execute(kCreateTabState)) {
    return false;
  }
  // Forward migration: advance an older (v1, visits-only) journal to v2 after
  // the idempotent `tab_state` creation above, leaving `visits` and its rows
  // intact.
  if (meta.GetVersionNumber() < kCurrentVersion &&
      !meta.SetVersionNumber(kCurrentVersion)) {
    return false;
  }
  return true;
}

void VisitsStore::UpsertTabState(const TabStateRow& row) {
  if (!db_.is_open()) {
    return;
  }
  sql::Statement s(db_.GetUniqueStatement(
      "INSERT OR REPLACE INTO "
      "tab_state(restore_key,closed,window_id,last_known_url) "
      "VALUES(?,?,?,?)"));
  s.BindString(0, row.restore_key);
  s.BindInt64(1, row.closed ? 1 : 0);
  s.BindInt64(2, row.window_id);
  s.BindString(3, row.last_known_url);
  s.Run();
}

std::optional<TabStateRow> VisitsStore::GetTabState(
    const std::string& restore_key) {
  if (!db_.is_open()) {
    return std::nullopt;
  }
  sql::Statement s(db_.GetUniqueStatement(
      "SELECT restore_key,closed,window_id,last_known_url FROM tab_state "
      "WHERE restore_key=?"));
  s.BindString(0, restore_key);
  if (!s.Step()) {
    return std::nullopt;
  }
  TabStateRow row;
  row.restore_key = s.ColumnString(0);
  row.closed = s.ColumnInt64(1) != 0;
  row.window_id = s.ColumnInt64(2);
  row.last_known_url = s.ColumnString(3);
  return row;
}

std::vector<TabStateRow> VisitsStore::ReadTabStates() {
  std::vector<TabStateRow> rows;
  if (!db_.is_open()) {
    return rows;
  }
  sql::Statement s(db_.GetUniqueStatement(
      "SELECT restore_key,closed,window_id,last_known_url FROM tab_state "
      "ORDER BY restore_key ASC"));
  while (s.Step()) {
    TabStateRow row;
    row.restore_key = s.ColumnString(0);
    row.closed = s.ColumnInt64(1) != 0;
    row.window_id = s.ColumnInt64(2);
    row.last_known_url = s.ColumnString(3);
    rows.push_back(std::move(row));
  }
  return rows;
}

void VisitsStore::Append(const std::string& url, base::Time visited_at) {
  if (!db_.is_open()) {
    return;
  }
  sql::Transaction transaction(&db_);
  if (!transaction.Begin()) {
    return;
  }

  sql::Statement insert(
      db_.GetUniqueStatement("INSERT INTO visits(url,visited_at) VALUES(?,?)"));
  insert.BindString(0, url);
  insert.BindInt64(1, ToStorage(visited_at));
  if (!insert.Run()) {
    return;
  }

  // FIFO trim: keep only the newest kMaxVisits rows (oldest-out). At the
  // (kMaxVisits+1)th append this evicts exactly one row.
  sql::Statement trim(db_.GetUniqueStatement(
      "DELETE FROM visits WHERE id NOT IN "
      "(SELECT id FROM visits ORDER BY id DESC LIMIT ?)"));
  trim.BindInt64(0, static_cast<int64_t>(kMaxVisits));
  if (!trim.Run()) {
    return;
  }

  transaction.Commit();
}

std::vector<VisitRow> VisitsStore::ReadAll() {
  std::vector<VisitRow> rows;
  if (!db_.is_open()) {
    return rows;
  }
  sql::Statement s(db_.GetUniqueStatement(
      "SELECT id,url,visited_at FROM visits ORDER BY id ASC"));
  while (s.Step()) {
    VisitRow row;
    row.id = s.ColumnInt64(0);
    row.url = s.ColumnString(1);
    row.visited_at = FromStorage(s.ColumnInt64(2));
    rows.push_back(std::move(row));
  }
  return rows;
}

size_t VisitsStore::RowCount() {
  if (!db_.is_open()) {
    return 0;
  }
  sql::Statement s(db_.GetUniqueStatement("SELECT COUNT(*) FROM visits"));
  if (s.Step()) {
    return static_cast<size_t>(s.ColumnInt64(0));
  }
  return 0;
}

}  // namespace roamex::tab_visit

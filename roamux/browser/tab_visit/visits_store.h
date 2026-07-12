// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_BROWSER_TAB_VISIT_VISITS_STORE_H_
#define ROAMUX_BROWSER_TAB_VISIT_VISITS_STORE_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/time/time.h"
#include "sql/database.h"

namespace roamux::tab_visit {

// One settled-visit row.
struct VisitRow {
  int64_t id = 0;
  // The durable per-tab uid (roam-10) this visit belongs to (v3 / roam-26).
  // Empty for legacy pre-v3 rows.
  std::string tab_uid;
  std::string url;
  base::Time visited_at;
};

// One persisted tab-state row (roam-22 / I-4.2), keyed by the durable
// restore_key. These are the PERSISTED columns only — the volatile
// liveSessionId/liveTabs are never stored here (the persisted-vs-volatile
// split, J3).
struct TabStateRow {
  std::string restore_key;
  bool closed = false;
  int64_t window_id = 0;
  std::string last_known_url;
};

// The writable, append-only SQLite `visits` store backing the settled-visit
// journal (roam-21 / I-4.1). It is the FIRST writable overlay-owned SQLite DB.
//
// Contract: rows are only ever appended (never mutated), and the table is
// FIFO-capped at kMaxVisits — appending the (kMaxVisits+1)th row evicts the
// oldest. The store is opened either on disk (regular profile) or in memory
// (OTR/incognito — never touches disk). A corrupt/incompatible database is
// razed and reset rather than failing the browser.
//
// Blocking by construction (SQLite I/O): the owner runs it on a MayBlock
// sequence (see SettledVisitJournalService's base::SequenceBound); it must
// never be used on the UI thread.
class VisitsStore {
 public:
  // The FIFO cap on the number of retained visits.
  static constexpr size_t kMaxVisits = 999;

  VisitsStore();
  VisitsStore(const VisitsStore&) = delete;
  VisitsStore& operator=(const VisitsStore&) = delete;
  ~VisitsStore();

  // Opens (or creates) the on-disk database at `db_path`. Returns true on
  // success. A corrupt DB is razed and reopened empty.
  bool Open(const base::FilePath& db_path);

  // Opens a fresh in-memory database (OTR): nothing is ever written to disk.
  bool OpenInMemory();

  // Appends a visit and trims the table back to kMaxVisits (oldest-out), in one
  // transaction. No-op if the store is not open.
  void Append(const std::string& tab_uid,
              const std::string& url,
              base::Time visited_at);

  // All retained visits, oldest-first (ascending id).
  std::vector<VisitRow> ReadAll();

  // The number of retained visits.
  size_t RowCount();

  // roam-28 (I-4.8): Clear-Browsing-Data. In one transaction, deletes settled
  // visits whose `visited_at` is in [begin, end) (begin-inclusive,
  // end-exclusive — matching Chromium history; a null `begin` / max `end` is an
  // unbounded side, so null-begin + max-end truncates all), then GCs orphaned
  // CLOSED `tab_state` rows (closed=1 whose uid no longer has any surviving
  // visit). LIVE rows (closed=0) are never touched. No-op if the store is not
  // open.
  void ClearForBrowsingDataRemoval(base::Time begin, base::Time end);

  // Inserts-or-replaces the persisted state for `row.restore_key` (mutable, not
  // append-only). No-op if the store is not open.
  void UpsertTabState(const TabStateRow& row);

  // The persisted state for `restore_key`, or nullopt if absent/not open.
  std::optional<TabStateRow> GetTabState(const std::string& restore_key);

  // All persisted tab states (by restore_key).
  std::vector<TabStateRow> ReadTabStates();

  bool is_open() const { return db_.is_open(); }

 private:
  // Initializes/migrates the schema (MetaTable, the `visits` table, and the
  // `tab_state` sidecar), advancing an older journal to the current version. A
  // corrupt/incompatible database is handled by the caller (Open recreates it).
  bool InitSchema();

  sql::Database db_;
};

}  // namespace roamux::tab_visit

#endif  // ROAMUX_BROWSER_TAB_VISIT_VISITS_STORE_H_

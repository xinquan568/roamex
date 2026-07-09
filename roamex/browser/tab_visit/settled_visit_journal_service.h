// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMEX_BROWSER_TAB_VISIT_SETTLED_VISIT_JOURNAL_SERVICE_H_
#define ROAMEX_BROWSER_TAB_VISIT_SETTLED_VISIT_JOURNAL_SERVICE_H_

#include <optional>
#include <string>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/containers/flat_set.h"
#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/threading/sequence_bound.h"
#include "components/keyed_service/core/keyed_service.h"
#include "roamex/browser/tab_visit/visits_store.h"

class GURL;

namespace roamex::tab_visit {

// The per-profile settled-visit journal service (roam-21 / I-4.1) — the E4
// storage substrate the later links navigate over. It owns a VisitsStore on a
// MayBlock sequence (SQLite I/O never runs on the UI thread) and exposes an
// append + read API on the UI thread.
//
// The factory decides the store's location: a regular profile passes an on-disk
// `db_path`; an OTR/incognito profile passes `in_memory=true` so nothing ever
// touches disk. When `roamex::features::kTabVisitNav` is disabled the service
// is inert — it opens NO store, so a disabled build leaves zero on-disk
// footprint.
class SettledVisitJournalService : public KeyedService {
 public:
  // `in_memory` true (OTR) opens an in-memory DB and ignores `db_path`; false
  // (regular) opens/creates the DB at `db_path`.
  SettledVisitJournalService(bool in_memory, base::FilePath db_path);
  SettledVisitJournalService(const SettledVisitJournalService&) = delete;
  SettledVisitJournalService& operator=(const SettledVisitJournalService&) =
      delete;
  ~SettledVisitJournalService() override;

  // Appends a settled visit (async; committed on the store sequence). No-op
  // when the feature is disabled.
  void RecordVisit(const GURL& url);

  // Reads all retained visits (oldest-first) and runs `callback` on the
  // caller's sequence. Returns an empty vector when the feature is disabled.
  void GetVisits(base::OnceCallback<void(std::vector<VisitRow>)> callback);

  // --- Tab-state sidecar (roam-22 / I-4.2) ---

  // Mirrors the PERSISTED tab state to disk (async). No-op when disabled.
  void SetTabState(TabStateRow row);

  // Sets/clears the VOLATILE live-session id for a tab (in-memory only — never
  // persisted, dropped on shutdown; the persisted-vs-volatile split, J3).
  void SetLiveSessionId(std::string restore_key, std::string live_session_id);
  void ClearLiveSessionId(const std::string& restore_key);

  // The VOLATILE set of currently-live tabs (in-memory only).
  void AddLiveTab(std::string restore_key);
  void RemoveLiveTab(const std::string& restore_key);
  bool HasLiveTab(const std::string& restore_key) const;

  // Read-through: returns the PERSISTED tab state (from the store, so it
  // survives reopen) merged with the VOLATILE live-session id (from memory,
  // empty in a fresh process). Empty/nullopt when the feature is disabled.
  void GetTabState(
      const std::string& restore_key,
      base::OnceCallback<void(std::optional<TabStateRow>,
                              std::optional<std::string> live_session_id)>
          callback);

  // KeyedService:
  void Shutdown() override;

 private:
  // Null (unbound) when the feature is disabled — the inert state.
  base::SequenceBound<VisitsStore> store_;

  // Volatile, in-memory-only state (UI thread) — never persisted.
  base::flat_map<std::string, std::string> live_session_ids_;
  base::flat_set<std::string> live_tabs_;
};

}  // namespace roamex::tab_visit

#endif  // ROAMEX_BROWSER_TAB_VISIT_SETTLED_VISIT_JOURNAL_SERVICE_H_

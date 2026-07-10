// SPDX-License-Identifier: Apache-2.0
#include "roamex/browser/tab_visit/settled_visit_journal_service.h"

#include <utility>

#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "roamex/common/roamex_features.h"
#include "url/gurl.h"

namespace roamex::tab_visit {

SettledVisitJournalService::SettledVisitJournalService(bool in_memory,
                                                       base::FilePath db_path) {
  // Inert when the epic's flag is off: no store is opened, so no journal DB (on
  // disk or in memory) is ever created.
  if (!base::FeatureList::IsEnabled(features::kTabVisitNav)) {
    return;
  }
  store_ = base::SequenceBound<VisitsStore>(
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE}));
  // Open on the store sequence; the bool result is not needed on the UI thread
  // (a failed open leaves the store closed, so reads return empty).
  if (in_memory) {
    store_.AsyncCall(&VisitsStore::OpenInMemory).Then(base::BindOnce([](bool) {
    }));
  } else {
    store_.AsyncCall(&VisitsStore::Open)
        .WithArgs(std::move(db_path))
        .Then(base::BindOnce([](bool) {}));
  }
}

SettledVisitJournalService::~SettledVisitJournalService() = default;

void SettledVisitJournalService::RecordVisit(const std::string& tab_uid,
                                             const GURL& url) {
  if (store_.is_null()) {
    return;
  }
  store_.AsyncCall(&VisitsStore::Append)
      .WithArgs(tab_uid, url.spec(), base::Time::Now());
}

void SettledVisitJournalService::GetVisits(
    base::OnceCallback<void(std::vector<VisitRow>)> callback) {
  if (store_.is_null()) {
    std::move(callback).Run({});
    return;
  }
  store_.AsyncCall(&VisitsStore::ReadAll).Then(std::move(callback));
}

void SettledVisitJournalService::ClearBrowsingData(base::Time begin,
                                                   base::Time end,
                                                   base::OnceClosure done) {
  if (store_.is_null()) {
    // Feature disabled (unbound store) — still run the completion token exactly
    // once so BrowsingDataRemover's "done" fires.
    std::move(done).Run();
    return;
  }
  store_.AsyncCall(&VisitsStore::ClearForBrowsingDataRemoval)
      .WithArgs(begin, end)
      .Then(std::move(done));
}

void SettledVisitJournalService::GetAllTabStates(
    base::OnceCallback<void(std::vector<TabStateRow>)> callback) {
  if (store_.is_null()) {
    std::move(callback).Run({});
    return;
  }
  store_.AsyncCall(&VisitsStore::ReadTabStates).Then(std::move(callback));
}

void SettledVisitJournalService::SetTabState(TabStateRow row) {
  if (store_.is_null()) {
    return;
  }
  store_.AsyncCall(&VisitsStore::UpsertTabState).WithArgs(std::move(row));
}

void SettledVisitJournalService::SetLiveSessionId(std::string restore_key,
                                                  std::string live_session_id) {
  live_session_ids_[std::move(restore_key)] = std::move(live_session_id);
}

void SettledVisitJournalService::ClearLiveSessionId(
    const std::string& restore_key) {
  live_session_ids_.erase(restore_key);
}

void SettledVisitJournalService::AddLiveTab(std::string restore_key) {
  live_tabs_.insert(std::move(restore_key));
}

void SettledVisitJournalService::RemoveLiveTab(const std::string& restore_key) {
  live_tabs_.erase(restore_key);
}

bool SettledVisitJournalService::HasLiveTab(
    const std::string& restore_key) const {
  return live_tabs_.contains(restore_key);
}

void SettledVisitJournalService::GetTabState(
    const std::string& restore_key,
    base::OnceCallback<void(std::optional<TabStateRow>,
                            std::optional<std::string> live_session_id)>
        callback) {
  if (store_.is_null()) {
    std::move(callback).Run(std::nullopt, std::nullopt);
    return;
  }
  // Snapshot the VOLATILE live-session id NOW (UI thread) and bind it BY VALUE
  // into the continuation, so the reply needs no `this` and is safe across
  // Shutdown/destruction.
  std::optional<std::string> live;
  if (auto it = live_session_ids_.find(restore_key);
      it != live_session_ids_.end()) {
    live = it->second;
  }
  store_.AsyncCall(&VisitsStore::GetTabState)
      .WithArgs(restore_key)
      .Then(base::BindOnce(
          [](std::optional<std::string> live,
             base::OnceCallback<void(std::optional<TabStateRow>,
                                     std::optional<std::string>)> cb,
             std::optional<TabStateRow> persisted) {
            std::move(cb).Run(std::move(persisted), std::move(live));
          },
          std::move(live), std::move(callback)));
}

void SettledVisitJournalService::Shutdown() {
  store_.Reset();
}

}  // namespace roamex::tab_visit

// SPDX-License-Identifier: Apache-2.0
#include "roamux/browser/tab_visit/tab_visit_traversal_coordinator.h"

#include <utility>

#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_user_gesture_details.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "roamux/browser/tab_visit/settled_visit_journal_factory.h"
#include "roamux/browser/tab_visit/settled_visit_journal_service.h"
#include "roamux/browser/tab_visit/visit_url_filter.h"
#include "roamux/browser/tabs/tab_uid_tab_helper.h"
#include "roamux/common/roamux_features.h"
#include "roamux/common/roamux_prefs.h"
#include "ui/base/base_window.h"
#include "url/gurl.h"

namespace roamux::tab_visit {

namespace {
// Fallback settle delay when a modifier release is never observed (§6.7 —
// release-primary, debounce fallback). Each step pushes the deadline out.
constexpr base::TimeDelta kSettleDebounce = base::Milliseconds(900);
}  // namespace

TabVisitTraversalCoordinator::TabVisitTraversalCoordinator(Profile* profile)
    : profile_(profile) {
  // roam-26: reload the persisted uid-journal into the MRU commit-log so
  // traversal survives a restart. DEFER the load to the next UI-thread task —
  // reaching another KeyedService (the journal factory) here in the ctor would
  // re-enter GetServiceForBrowserContext during this service's own construction
  // and deadlock profile init.
  if (base::FeatureList::IsEnabled(features::kTabVisitNav)) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&TabVisitTraversalCoordinator::LoadPersistedJournal,
                       weak_factory_.GetWeakPtr()));
  }
}

TabVisitTraversalCoordinator::~TabVisitTraversalCoordinator() = default;

void TabVisitTraversalCoordinator::LoadPersistedJournal() {
  if (SettledVisitJournalService* journal =
          SettledVisitJournalFactory::GetForProfile(profile_)) {
    journal->GetVisits(
        base::BindOnce(&TabVisitTraversalCoordinator::OnJournalLoaded,
                       weak_factory_.GetWeakPtr()));
    // roam-27: also load the persisted closed-tab rows so tabs closed before a
    // restart are reopenable when the pref is on.
    LoadReopenableFromSidecar();
  } else {
    OnJournalLoaded({});  // Nothing persisted (disabled / OTR) — mark loaded.
  }
}

void TabVisitTraversalCoordinator::OnJournalLoaded(std::vector<VisitRow> rows) {
  // Persisted history is OLDER than anything appended at runtime during the
  // async read: [persisted…] ++ [runtime…]. Drop empty-uid (legacy) rows.
  std::vector<std::string> merged;
  merged.reserve(rows.size() + settled_uid_log_.size());
  for (const VisitRow& row : rows) {
    if (!row.tab_uid.empty()) {
      merged.push_back(row.tab_uid);
    }
  }
  for (const std::string& uid : settled_uid_log_) {
    merged.push_back(uid);
  }
  // Coalesce consecutive duplicates (incl. the persisted/runtime boundary).
  settled_uid_log_.clear();
  for (std::string& uid : merged) {
    if (settled_uid_log_.empty() || settled_uid_log_.back() != uid) {
      settled_uid_log_.push_back(std::move(uid));
    }
  }
  journal_loaded_ = true;
  journal_loaded_callbacks_.Notify();
}

base::CallbackListSubscription
TabVisitTraversalCoordinator::AddJournalLoadedCallback(
    base::RepeatingClosure cb) {
  if (journal_loaded_) {
    cb.Run();  // Already loaded — refresh immediately.
    return {};
  }
  return journal_loaded_callbacks_.Add(std::move(cb));
}

void TabVisitTraversalCoordinator::OnReachabilityMaybeChanged() {
  if (journal_loaded_) {
    journal_loaded_callbacks_.Notify();  // Refresh per-window command state.
  }
}

// static
std::string TabVisitTraversalCoordinator::UidOf(
    content::WebContents* web_contents) {
  if (!web_contents) {
    return std::string();
  }
  tabs::TabUidTabHelper* helper =
      tabs::TabUidTabHelper::FromWebContents(web_contents);
  return helper ? helper->uid() : std::string();
}

base::flat_set<std::string> TabVisitTraversalCoordinator::CollectLiveUids()
    const {
  base::flat_set<std::string> uids;
  for (BrowserWindowInterface* browser : GetAllBrowserWindowInterfaces()) {
    if (browser->GetProfile() != profile_) {
      continue;
    }
    TabStripModel* model = browser->GetTabStripModel();
    for (int i = 0; i < model->count(); ++i) {
      std::string uid = UidOf(model->GetWebContentsAt(i));
      if (!uid.empty()) {
        uids.insert(std::move(uid));
      }
    }
  }
  return uids;
}

std::optional<TabVisitTraversalCoordinator::TabLocation>
TabVisitTraversalCoordinator::ResolveUid(const std::string& uid) const {
  if (uid.empty()) {
    return std::nullopt;
  }
  for (BrowserWindowInterface* browser : GetAllBrowserWindowInterfaces()) {
    if (browser->GetProfile() != profile_) {
      continue;
    }
    TabStripModel* model = browser->GetTabStripModel();
    for (int i = 0; i < model->count(); ++i) {
      content::WebContents* wc = model->GetWebContentsAt(i);
      if (UidOf(wc) == uid) {
        return TabLocation{browser, wc, i};
      }
    }
  }
  return std::nullopt;
}

void TabVisitTraversalCoordinator::RecordSettledUid(const std::string& uid) {
  if (uid.empty()) {
    return;
  }
  // Coalesce by uid: no consecutive duplicate tail (§6.3).
  if (!settled_uid_log_.empty() && settled_uid_log_.back() == uid) {
    return;
  }
  settled_uid_log_.push_back(uid);
}

void TabVisitTraversalCoordinator::EnsureGesture() {
  if (traversal_active_) {
    return;
  }
  anchor_uid_ =
      settled_uid_log_.empty() ? std::string() : settled_uid_log_.back();
  controller_.BeginGesture(settled_uid_log_, CollectReachableUids());
  traversal_active_ = true;
}

void TabVisitTraversalCoordinator::ActivateLocation(const TabLocation& loc) {
  loc.browser->GetTabStripModel()->ActivateTabAt(
      loc.index, TabStripUserGestureDetails(
                     TabStripUserGestureDetails::GestureType::kNone));
  // Focus the owning window (the landing may live in another window, §6.5).
  loc.browser->GetWindow()->Activate();
}

void TabVisitTraversalCoordinator::StepAndActivate(bool forward) {
  std::optional<TabIdentity> uid =
      forward ? controller_.Forward() : controller_.Back();
  if (uid) {
    std::optional<TabLocation> loc = ResolveUid(*uid);
    if (!loc && TryReopenClosedUid(*uid)) {
      loc = ResolveUid(*uid);  // Now live (reopened).
    }
    if (loc) {
      ActivateLocation(*loc);
    }
  }
  // (Re)arm the debounce fallback; each step pushes the settle deadline out.
  debounce_timer_.Start(FROM_HERE, kSettleDebounce,
                        base::BindOnce(&TabVisitTraversalCoordinator::Settle,
                                       base::Unretained(this)));
}

void TabVisitTraversalCoordinator::StepBack() {
  if (!base::FeatureList::IsEnabled(features::kTabVisitNav)) {
    return;
  }
  // Ignore a step that cannot land — do NOT start (or extend) a gesture on a
  // no-op direction (guards against stale command enablement, F1).
  if (!CanGoBack()) {
    return;
  }
  EnsureGesture();
  StepAndActivate(/*forward=*/false);
}

void TabVisitTraversalCoordinator::StepForward() {
  if (!base::FeatureList::IsEnabled(features::kTabVisitNav)) {
    return;
  }
  if (!CanGoForward()) {
    return;
  }
  EnsureGesture();
  StepAndActivate(/*forward=*/true);
}

void TabVisitTraversalCoordinator::Settle() {
  if (!traversal_active_) {
    return;
  }
  debounce_timer_.Stop();
  // Capture the settled uid; the pure controller clears its state here, but
  // `traversal_active_` STAYS true so the bridge remains suppressed across the
  // activation below (no double append).
  std::optional<TabIdentity> uid = controller_.Settle();
  // Commit exactly ONE settled visit — but only if the gesture actually landed
  // on a DIFFERENT tab than the anchor it started from. Returning to (or never
  // moving off) the anchor is not a new visit, so it must not re-append the
  // current tab (F1). A landed-but-now-closed tab commits nothing.
  if (uid && *uid != anchor_uid_) {
    std::optional<TabLocation> loc = ResolveUid(*uid);
    if (!loc && TryReopenClosedUid(*uid)) {
      loc = ResolveUid(*uid);  // Reopened the closed landing (§6.6).
    }
    if (loc) {
      ActivateLocation(*loc);
      RecordSettledUid(*uid);
      GURL url = loc->web_contents->GetLastCommittedURL();
      // roam-27: an EVICTION-fallback reopen appends a blank WebContents and
      // navigates asynchronously, so by Settle time its committed URL is not
      // yet recordable. Fall back to the authoritative last_known_url captured
      // when the tab was closed, so the settled visit is still persisted
      // (exactly one append per gesture, survives a restart).
      if (!IsRecordableVisitUrl(url)) {
        auto it = reopened_url_by_uid_.find(*uid);
        if (it != reopened_url_by_uid_.end()) {
          url = GURL(it->second);
        }
      }
      if (IsRecordableVisitUrl(url)) {
        if (SettledVisitJournalService* journal =
                SettledVisitJournalFactory::GetForProfile(profile_)) {
          // roam-26: uid-keyed, so the settled visit survives a restart.
          journal->RecordVisit(*uid, url);  // Exactly one append per gesture.
        }
      }
    }
  }
  // Clear LAST — after the settle activation has been suppressed.
  reopened_url_by_uid_.clear();  // Per-gesture; reset for the next gesture.
  traversal_active_ = false;
}

void TabVisitTraversalCoordinator::CancelGesture() {
  debounce_timer_.Stop();
  if (traversal_active_) {
    controller_.Settle();  // Drop the pure state without committing.
    reopened_url_by_uid_.clear();
    traversal_active_ = false;
  }
}

bool TabVisitTraversalCoordinator::CanGoBack() const {
  if (traversal_active_) {
    return controller_.CanGoBack();
  }
  return CanBeginBackTraversal(settled_uid_log_, CollectReachableUids());
}

bool TabVisitTraversalCoordinator::CanGoForward() const {
  // Before a gesture the tail is already the most-recent tab (nothing newer).
  return traversal_active_ && controller_.CanGoForward();
}

bool TabVisitTraversalCoordinator::ReopenClosedEnabled() const {
  return profile_ && profile_->GetPrefs() &&
         profile_->GetPrefs()->GetBoolean(prefs::kReopenClosed);
}

base::flat_set<std::string> TabVisitTraversalCoordinator::CollectReachableUids()
    const {
  base::flat_set<std::string> uids = CollectLiveUids();
  if (ReopenClosedEnabled()) {
    for (const auto& [uid, row] : reopenable_) {
      uids.insert(uid);  // closed-but-reopenable.
    }
  }
  return uids;
}

void TabVisitTraversalCoordinator::SetReopenFn(ReopenFn fn) {
  reopen_fn_ = std::move(fn);
}

void TabVisitTraversalCoordinator::AddReopenable(const TabStateRow& row) {
  if (row.restore_key.empty()) {
    return;
  }
  recently_reopened_.erase(
      row.restore_key);  // A fresh close clears the tombstone.
  reopenable_[row.restore_key] = row;
  OnReachabilityMaybeChanged();  // Back enablement may have changed
                                 // (reopen-on).
}

void TabVisitTraversalCoordinator::PrepareForBrowsingDataClear() {
  // Called SYNCHRONOUSLY at the START of a clear, BEFORE the async store clear
  // is posted (roam-28 F1). This closes a TOCTOU race: if a gesture were still
  // active when the store-clear task is queued, a modifier-release / debounce
  // Settle could run in the clear window and post RecordVisit (Append) AFTER
  // the Delete on the store sequence — re-appending the just-cleared landed
  // URL. Cancelling here (no commit: stops the debounce timer, drops controller
  // state, clears traversal_active_ + reopened_url_by_uid_) makes any such
  // Settle a no-op.
  CancelGesture();
  // Also drop the in-memory closed-tab records NOW — they carry last_known_url,
  // so a cleared closed tab must not remain reopenable from memory (roam-28
  // F2), and a new gesture starting in the clear window must not reopen one.
  // This touches ONLY closed-tab state: live-tab uids are not in reopenable_,
  // and the service's volatile live sets are untouched (live identity survives,
  // §6.9).
  reopenable_.clear();
  reopened_url_by_uid_.clear();
  // Invalidate any in-flight sidecar load: a GetAllTabStates read posted BEFORE
  // this clear reflects the pre-clear sidecar, so its reply must not resurrect
  // cleared closed-tab rows (roam-28 N1).
  ++reopenable_generation_;
}

void TabVisitTraversalCoordinator::OnBrowsingDataCleared() {
  // Called AFTER the async store clear (visits deleted + orphaned closed
  // tab_state GC'd) completes. The gesture + in-memory URL caches were already
  // dropped synchronously in PrepareForBrowsingDataClear; here we only resync
  // the reopenable cache to the now-GC'd sidecar (best-effort, async) so any
  // closed rows that survived a time-ranged GC become reopenable again.
  LoadReopenableFromSidecar();
  OnReachabilityMaybeChanged();
}

BrowserWindowInterface* TabVisitTraversalCoordinator::ReopenTargetWindow(
    const TabStateRow& row) const {
  // Prefer the tab's ORIGINAL window if it is still live (so an eviction-
  // fallback tab lands back where it was closed from); else the first normal
  // window of the profile. For an EXACT restore the target only supplies the
  // LiveTabContext (WindowOpenDisposition::UNKNOWN honors the entry's own saved
  // placement regardless), but the fallback appends into this window, so the
  // original-window match matters there.
  BrowserWindowInterface* fallback = nullptr;
  for (BrowserWindowInterface* browser : GetAllBrowserWindowInterfaces()) {
    if (browser->GetProfile() != profile_ ||
        browser->GetType() != BrowserWindowInterface::TYPE_NORMAL) {
      continue;
    }
    if (row.window_id != 0 && browser->GetSessionID().id() == row.window_id) {
      return browser;  // The original window is still live.
    }
    if (!fallback) {
      fallback = browser;
    }
  }
  return fallback;
}

bool TabVisitTraversalCoordinator::TryReopenClosedUid(const std::string& uid) {
  if (!ReopenClosedEnabled() || reopen_fn_.is_null()) {
    return false;
  }
  auto it = reopenable_.find(uid);
  if (it == reopenable_.end()) {
    return false;
  }
  const TabStateRow row = it->second;  // Copy before the map is mutated.
  BrowserWindowInterface* target = ReopenTargetWindow(row);
  if (!target || !reopen_fn_.Run(target, uid, row.last_known_url)) {
    return false;  // Reopen failed — leave the sidecar closed.
  }
  // Remember the intended URL so Settle can persist the visit even when a
  // fallback reopen's navigation has not committed yet (F1).
  reopened_url_by_uid_[uid] = row.last_known_url;
  RebindSidecar(row);
  return true;
}

void TabVisitTraversalCoordinator::RebindSidecar(const TabStateRow& row) {
  reopenable_.erase(row.restore_key);
  recently_reopened_.insert(
      row.restore_key);  // Async reload must not resurrect.
  TabStateRow reopened = row;
  reopened.closed = false;
  if (SettledVisitJournalService* journal =
          SettledVisitJournalFactory::GetForProfile(profile_)) {
    journal->SetTabState(std::move(reopened));  // Single-sidecar rebind.
  }
}

void TabVisitTraversalCoordinator::LoadReopenableFromSidecar() {
  if (SettledVisitJournalService* journal =
          SettledVisitJournalFactory::GetForProfile(profile_)) {
    // Stamp the read with the current generation so a reply that predates a
    // browsing-data clear (which bumps the generation) is dropped as stale
    // (roam-28 N1) — otherwise it could resurrect just-cleared closed-tab URLs.
    journal->GetAllTabStates(
        base::BindOnce(&TabVisitTraversalCoordinator::OnReopenableLoaded,
                       weak_factory_.GetWeakPtr(), reopenable_generation_));
  }
}

void TabVisitTraversalCoordinator::OnReopenableLoaded(
    int generation,
    std::vector<TabStateRow> rows) {
  if (generation != reopenable_generation_) {
    // A browsing-data clear happened after this read was posted — the rows
    // reflect the PRE-clear sidecar and must not repopulate reopenable_.
    return;
  }
  for (TabStateRow& row : rows) {
    if (!row.closed || row.restore_key.empty() ||
        recently_reopened_.contains(row.restore_key)) {
      continue;  // Open, keyless, or reopened-this-session (tombstone).
    }
    // Do NOT clobber a synchronously-added row (sync is authoritative).
    reopenable_.try_emplace(row.restore_key, row);
  }
  OnReachabilityMaybeChanged();
}

void TabVisitTraversalCoordinator::Shutdown() {
  CancelGesture();
}

}  // namespace roamux::tab_visit

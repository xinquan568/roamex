// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMEX_BROWSER_TAB_VISIT_TAB_VISIT_TRAVERSAL_COORDINATOR_H_
#define ROAMEX_BROWSER_TAB_VISIT_TAB_VISIT_TRAVERSAL_COORDINATOR_H_

#include <optional>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/containers/flat_map.h"
#include "base/containers/flat_set.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "components/keyed_service/core/keyed_service.h"
#include "roamex/browser/tab_visit/tab_traversal_controller.h"
#include "roamex/browser/tab_visit/visits_store.h"

class BrowserWindowInterface;
class Profile;

namespace content {
class WebContents;
}

namespace roamex::tab_visit {

// The per-profile owner of the E4 traversal SESSION (roam-25 / I-4.5). It holds
// the single active roam-24 TabTraversalController (one gesture at a time per
// profile — so a Back/Forward that focuses another window of the same profile
// keeps the frozen order and cursor), the volatile session-scoped uid
// commit-log that the MRU is derived from, and the "traversal active"
// suppression flag the roam-23 bridge consults so intermediate previews are not
// appended.
//
// The Back/Forward STEPS arrive from the IDC_TAB_VISIT_BACK/FORWARD commands
// (per window). The SETTLE trigger is either a modifier release forwarded by a
// per-window gesture watcher (release-primary) or this coordinator's own
// debounce timer (fallback) — the timer lives here, not in a window, so a
// cross-window landing or a window close cannot strand the session.
//
// Identity is a durable per-tab uid (roam-10 TabUidTabHelper) — never a URL.
// Volatile only: cross-restart persistence + reopen of closed tabs are I-4.6 /
// I-4.7. Inert unless `roamex::features::kTabVisitNav` is enabled.
class TabVisitTraversalCoordinator : public KeyedService {
 public:
  explicit TabVisitTraversalCoordinator(Profile* profile);
  TabVisitTraversalCoordinator(const TabVisitTraversalCoordinator&) = delete;
  TabVisitTraversalCoordinator& operator=(const TabVisitTraversalCoordinator&) =
      delete;
  ~TabVisitTraversalCoordinator() override;

  // Appends `uid` to the volatile settled commit-log, coalescing when it equals
  // the current tail (no consecutive duplicate, §6.3). Called by the roam-23
  // bridge on a NORMAL (non-gesture) settled activation, and by Settle().
  void RecordSettledUid(const std::string& uid);

  // Back/Forward step (from the commands). Begins a gesture on the first step
  // (freezing the MRU derived from the commit-log, reachable = the profile's
  // live tab uids), activates + focuses the landed tab (across windows), and
  // (re)arms the debounce timer. No-op if the feature is disabled.
  void StepBack();
  void StepForward();

  // Ends the gesture: commits EXACTLY ONE settled visit for the landed tab
  // (log + journal) while the bridge is still suppressed, then clears the
  // suppression flag last. No-op if no gesture is active. This is the
  // release-primary settle path and the debounce-timer target.
  void Settle();

  // Ends the gesture WITHOUT committing (Escape / window close / interruption).
  // Always clears the suppression flag so the bridge is never left suppressed.
  void CancelGesture();

  // True while a gesture is in flight — the roam-23 bridge suppresses its
  // per-activation commit while this holds. Backed by a dedicated bool (NOT the
  // pure controller's gesture_active(), which clears mid-Settle).
  bool IsTraversalActive() const { return traversal_active_; }

  // roam-26: true once the persisted uid-journal has been loaded into the MRU
  // commit-log at startup. `AddJournalLoadedCallback` runs `cb` immediately if
  // the load already completed, else on completion — a per-window watcher uses
  // it to refresh Back/Forward command enablement after the async load.
  bool IsJournalLoaded() const { return journal_loaded_; }
  base::CallbackListSubscription AddJournalLoadedCallback(
      base::RepeatingClosure cb);

  // roam-26: the reachable-live-uid set may have changed (e.g. a restored tab
  // just registered its durable uid), so Back/Forward enablement should be
  // recomputed. Re-fires the journal-loaded callbacks (which refresh per-window
  // command state) once the journal has loaded. Called by the bridge on tab
  // insert/removal. No-op before the load.
  void OnReachabilityMaybeChanged();

  // roam-27 (I-4.7): the reopen action. `fn(target, uid, last_known_url)`
  // reopens the closed tab with durable `uid` into `target`'s window (exact via
  // TabRestoreService, else a fresh tab at `last_known_url` re-stamped with the
  // old uid), returning success. It lives in a //chrome/browser/ui shim (needs
  // Browser); the ui layer injects it here so the ui/tabs coordinator can
  // invoke reopen without a GN cycle.
  using ReopenFn = base::RepeatingCallback<
      bool(BrowserWindowInterface*, const std::string&, const std::string&)>;
  void SetReopenFn(ReopenFn fn);
  bool has_reopen_fn() const { return !reopen_fn_.is_null(); }

  // roam-27: record a just-closed tab as reopenable (its full sidecar row, so
  // last_known_url/window_id are available synchronously). Called by the bridge
  // at close time, before the async sidecar persist — so a gesture immediately
  // after a close sees the uid as reopenable. Clears any stale tombstone.
  void AddReopenable(const TabStateRow& row);

  // roam-28 (I-4.8): the user is clearing browsing data. Two-phase so an
  // in-flight gesture cannot re-append a just-cleared URL during the async
  // store-clear window (F1):
  //  - PrepareForBrowsingDataClear() is invoked SYNCHRONOUSLY by the ui-layer
  //    clear hook BEFORE the async store clear is posted. It cancels any
  //    in-flight gesture (no commit) and drops the in-memory closed-tab records
  //    (reopenable_ carries last_known_url — F2) so a Settle or a new gesture
  //    in the clear window cannot append or reopen a cleared URL.
  //  - OnBrowsingDataCleared() is invoked AFTER the store clear completes; it
  //    only resyncs the reopenable cache to the now-GC'd sidecar.
  // Neither touches live-tab uids (reopenable_ holds only CLOSED tabs) nor any
  // volatile live set (live identity survives, §6.9).
  void PrepareForBrowsingDataClear();
  void OnBrowsingDataCleared();

  // For the ui-layer clear hook to weak-guard this per-profile service across
  // the async store-clear reply (roam-28).
  base::WeakPtr<TabVisitTraversalCoordinator> AsWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  // Command enablement (§6.3): during a gesture, delegate to the controller;
  // before a gesture, a Back is enabled iff the commit-log has a reachable uid
  // older than the tail, and a Forward is disabled.
  bool CanGoBack() const;
  bool CanGoForward() const;

  // KeyedService:
  void Shutdown() override;

 private:
  struct TabLocation {
    raw_ptr<BrowserWindowInterface> browser;
    raw_ptr<content::WebContents> web_contents;
    int index;
  };

  // Issues the one-shot persisted-journal read (posted from the ctor, off the
  // reentrant KeyedService construction path).
  void LoadPersistedJournal();

  // Merges the persisted uid-journal (older prefix) with any runtime appends
  // (newer suffix) that accrued during the async load, coalescing the boundary,
  // then notifies journal-loaded callbacks (roam-26 startup reload).
  void OnJournalLoaded(std::vector<VisitRow> rows);

  // Starts a gesture if none is active (freezes the MRU + reachable set).
  void EnsureGesture();
  // Steps then activates/focuses the landed tab; (re)arms the debounce timer.
  void StepAndActivate(bool forward);
  void ActivateLocation(const TabLocation& loc);

  // Resolve a uid to its live tab across the profile's Browsers, or nullopt.
  std::optional<TabLocation> ResolveUid(const std::string& uid) const;
  // The profile's currently-live tab uids.
  base::flat_set<std::string> CollectLiveUids() const;
  // The traversal reachable set: live uids, plus closed-but-reopenable uids
  // when `reopen_closed` is on. Reopen-off ⇒ exactly CollectLiveUids()
  // (unchanged).
  base::flat_set<std::string> CollectReachableUids() const;
  // True iff the `reopen_closed` pref is enabled for this profile.
  bool ReopenClosedEnabled() const;
  // Try to reopen a closed-reopenable uid (via the injected ReopenFn) and
  // rebind its sidecar row (closed=false). Returns true if a tab was reopened.
  bool TryReopenClosedUid(const std::string& uid);
  // Flip the sidecar row to closed=false and drop the uid from the reopenable
  // cache (adding a tombstone so a stale async reload can't resurrect it).
  void RebindSidecar(const TabStateRow& row);
  // Loads closed-reopenable rows from the persisted sidecar (startup /
  // refresh), skipping uids that were reopened this session (the tombstone
  // set).
  void LoadReopenableFromSidecar();
  // `generation` is the reopenable_generation_ captured when the read was
  // posted; a mismatch means a clear happened since, so the (pre-clear) rows
  // are dropped (roam-28 N1).
  void OnReopenableLoaded(int generation, std::vector<TabStateRow> rows);
  // The target window to reopen into (the row's window if live, else the first
  // normal window of the profile).
  BrowserWindowInterface* ReopenTargetWindow(const TabStateRow& row) const;
  // The uid of a tab's WebContents (roam-10 helper), or empty.
  static std::string UidOf(content::WebContents* web_contents);

  const raw_ptr<Profile> profile_;
  TabTraversalController controller_;
  std::vector<std::string> settled_uid_log_;  // Volatile, session-scoped.
  // The tab the gesture started from (the log tail). A settle that lands back
  // on it is not a new visit — prevents re-appending the current tab (§6.3).
  std::string anchor_uid_;
  bool traversal_active_ = false;
  bool journal_loaded_ = false;  // roam-26: persisted MRU reloaded at startup.
  base::RepeatingClosureList journal_loaded_callbacks_;
  // roam-27: closed-but-reopenable tabs (uid -> full sidecar row) + a tombstone
  // set of uids reopened this session (so a stale async reload can't resurrect
  // them) + the injected reopen action (lives in a //chrome/browser/ui shim).
  base::flat_map<std::string, TabStateRow> reopenable_;
  base::flat_set<std::string> recently_reopened_;
  // roam-28 (N1): bumped by PrepareForBrowsingDataClear; an async sidecar-load
  // reply whose captured generation no longer matches is dropped, so a read
  // posted before a clear cannot resurrect just-cleared closed-tab rows.
  int reopenable_generation_ = 0;
  // uid -> intended URL for tabs reopened during the CURRENT gesture. A
  // fallback-reopened tab's navigation is not committed by Settle time, so the
  // settled visit is recorded from this authoritative last_known_url instead of
  // the (still-empty) committed URL. Cleared at every gesture end.
  base::flat_map<std::string, std::string> reopened_url_by_uid_;
  ReopenFn reopen_fn_;
  base::OneShotTimer
      debounce_timer_;  // Owned here (per-profile), not per-window.
  base::WeakPtrFactory<TabVisitTraversalCoordinator> weak_factory_{this};
};

}  // namespace roamex::tab_visit

#endif  // ROAMEX_BROWSER_TAB_VISIT_TAB_VISIT_TRAVERSAL_COORDINATOR_H_

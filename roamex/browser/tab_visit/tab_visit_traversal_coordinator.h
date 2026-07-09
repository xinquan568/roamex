// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMEX_BROWSER_TAB_VISIT_TAB_VISIT_TRAVERSAL_COORDINATOR_H_
#define ROAMEX_BROWSER_TAB_VISIT_TAB_VISIT_TRAVERSAL_COORDINATOR_H_

#include <optional>
#include <string>
#include <vector>

#include "base/containers/flat_set.h"
#include "base/memory/raw_ptr.h"
#include "base/timer/timer.h"
#include "components/keyed_service/core/keyed_service.h"
#include "roamex/browser/tab_visit/tab_traversal_controller.h"

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

  // Starts a gesture if none is active (freezes the MRU + reachable set).
  void EnsureGesture();
  // Steps then activates/focuses the landed tab; (re)arms the debounce timer.
  void StepAndActivate(bool forward);
  void ActivateLocation(const TabLocation& loc);

  // Resolve a uid to its live tab across the profile's Browsers, or nullopt.
  std::optional<TabLocation> ResolveUid(const std::string& uid) const;
  // The profile's currently-live tab uids (the reachable set; reopen-off).
  base::flat_set<std::string> CollectLiveUids() const;
  // The uid of a tab's WebContents (roam-10 helper), or empty.
  static std::string UidOf(content::WebContents* web_contents);

  const raw_ptr<Profile> profile_;
  TabTraversalController controller_;
  std::vector<std::string> settled_uid_log_;  // Volatile, session-scoped.
  // The tab the gesture started from (the log tail). A settle that lands back
  // on it is not a new visit — prevents re-appending the current tab (§6.3).
  std::string anchor_uid_;
  bool traversal_active_ = false;
  base::OneShotTimer
      debounce_timer_;  // Owned here (per-profile), not per-window.
};

}  // namespace roamex::tab_visit

#endif  // ROAMEX_BROWSER_TAB_VISIT_TAB_VISIT_TRAVERSAL_COORDINATOR_H_

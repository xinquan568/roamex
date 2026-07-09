// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMEX_BROWSER_TAB_VISIT_TAB_VISIT_GESTURE_WATCHER_H_
#define ROAMEX_BROWSER_TAB_VISIT_TAB_VISIT_GESTURE_WATCHER_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "ui/events/event_observer.h"

class BrowserWindowInterface;

namespace views {
class EventMonitor;
}

namespace roamex::tab_visit {

// Per-window SETTLE detector for the E4 traversal gesture (roam-25 / I-4.5).
// The Back/Forward STEPS come from the IDC_TAB_VISIT_BACK/FORWARD commands;
// this watcher's only job is the SETTLE trigger — modifier-release-PRIMARY
// (§6.7, Q(i3)-A). It observes window key events and, while the per-profile
// TabVisitTraversalCoordinator has an active gesture, forwards:
//   * a release of the traversal modifier -> coordinator->Settle();
//   * Escape                              -> coordinator->CancelGesture().
// The debounce FALLBACK lives in the coordinator (per-profile), not here, so a
// cross-window landing or this window closing cannot strand the session. Owned
// per-window by BrowserWindowFeatures; only created when kTabVisitNav is on.
class TabVisitGestureWatcher : public ui::EventObserver {
 public:
  explicit TabVisitGestureWatcher(BrowserWindowInterface* browser);
  TabVisitGestureWatcher(const TabVisitGestureWatcher&) = delete;
  TabVisitGestureWatcher& operator=(const TabVisitGestureWatcher&) = delete;
  ~TabVisitGestureWatcher() override;

  // ui::EventObserver:
  void OnEvent(const ui::Event& event) override;

 private:
  // Refresh this window's Back/Forward command enablement after a gesture ends.
  void RefreshCommands();

  const raw_ptr<BrowserWindowInterface> browser_;
  std::unique_ptr<views::EventMonitor> event_monitor_;
};

}  // namespace roamex::tab_visit

#endif  // ROAMEX_BROWSER_TAB_VISIT_TAB_VISIT_GESTURE_WATCHER_H_

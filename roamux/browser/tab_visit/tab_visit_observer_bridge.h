// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_BROWSER_TAB_VISIT_TAB_VISIT_OBSERVER_BRIDGE_H_
#define ROAMUX_BROWSER_TAB_VISIT_TAB_VISIT_OBSERVER_BRIDGE_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/browser_tab_strip_tracker_delegate.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/keyed_service/core/keyed_service.h"

class BrowserTabStripTracker;
class BrowserWindowInterface;
class Profile;

namespace roamux::tab_visit {

// The E4 event source (roam-23 / I-4.3). A per-profile, EAGER KeyedService that
// observes tab activations across all of its profile's Browsers (via a
// profile-filtered BrowserTabStripTracker) and commits each SETTLED tab
// selection into that profile's settled-visit journal (roam-21/22) through
// SettledVisitJournalService::RecordVisit.
//
// The auto-successor — the tab the model auto-activates when the active tab is
// closed — is NEVER committed (the hard requirement). The commit decision lives
// in the pure `ShouldCommitActivation` predicate
// (tab_visit_activation_class.h); the bridge additionally owns URL gating
// (empty / about:blank / NTP are not recordable) because RecordVisit appends
// unconditionally.
//
// The factory is eager (ServiceIsCreatedWithBrowserContext), so the bridge
// starts observing at profile init without a process-global startup singleton.
// When `roamux::features::kTabVisitNav` is disabled the bridge is inert — it
// builds no tracker, observes nothing, and commits nothing. This class is
// Profile/UI-facing (needs //chrome/browser/ui) and is compiled into the
// upstream tab target via patch 0016 (the two-piece pattern of roam-21).
class TabVisitObserverBridge : public KeyedService,
                               public TabStripModelObserver,
                               public BrowserTabStripTrackerDelegate {
 public:
  explicit TabVisitObserverBridge(Profile* profile);
  TabVisitObserverBridge(const TabVisitObserverBridge&) = delete;
  TabVisitObserverBridge& operator=(const TabVisitObserverBridge&) = delete;
  ~TabVisitObserverBridge() override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

  // BrowserTabStripTrackerDelegate:
  bool ShouldTrackBrowser(BrowserWindowInterface* browser) override;

  // KeyedService:
  void Shutdown() override;

 private:
  raw_ptr<Profile> profile_;
  // Null (never constructed) when the feature is disabled — the inert state.
  std::unique_ptr<BrowserTabStripTracker> tracker_;
};

}  // namespace roamux::tab_visit

#endif  // ROAMUX_BROWSER_TAB_VISIT_TAB_VISIT_OBSERVER_BRIDGE_H_

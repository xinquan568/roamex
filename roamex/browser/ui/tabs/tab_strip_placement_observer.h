// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMEX_BROWSER_UI_TABS_TAB_STRIP_PLACEMENT_OBSERVER_H_
#define ROAMEX_BROWSER_UI_TABS_TAB_STRIP_PLACEMENT_OBSERVER_H_

#include "base/functional/callback.h"
#include "components/prefs/pref_change_registrar.h"

class PrefService;

namespace roamex::tabs {

// Fires `on_placement_changed` whenever roamex::prefs::kTabStripPosition
// changes — the live-switch half of I-1.2 (roam-7). The embedder (patch 0007's
// BrowserView hook) binds layout invalidation into the closure, keeping this
// class free of any //chrome or //ui/views dependency.
class TabStripPlacementObserver {
 public:
  TabStripPlacementObserver(PrefService* pref_service,
                            base::RepeatingClosure on_placement_changed);
  TabStripPlacementObserver(const TabStripPlacementObserver&) = delete;
  TabStripPlacementObserver& operator=(const TabStripPlacementObserver&) =
      delete;
  ~TabStripPlacementObserver();

 private:
  PrefChangeRegistrar registrar_;
};

}  // namespace roamex::tabs

#endif  // ROAMEX_BROWSER_UI_TABS_TAB_STRIP_PLACEMENT_OBSERVER_H_

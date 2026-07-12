// SPDX-License-Identifier: Apache-2.0
#include "roamux/browser/ui/tabs/tab_strip_placement_observer.h"

#include <utility>

#include "components/prefs/pref_service.h"
#include "roamux/common/roamux_prefs.h"

namespace roamux::tabs {

TabStripPlacementObserver::TabStripPlacementObserver(
    PrefService* pref_service,
    base::RepeatingClosure on_placement_changed) {
  registrar_.Init(pref_service);
  registrar_.Add(prefs::kTabStripPosition, std::move(on_placement_changed));
}

TabStripPlacementObserver::~TabStripPlacementObserver() = default;

}  // namespace roamux::tabs

// SPDX-License-Identifier: Apache-2.0
#include "roamex/common/tab_strip_placement.h"

#include <algorithm>

#include "base/feature_list.h"
#include "components/prefs/pref_service.h"
#include "roamex/common/roamex_features.h"
#include "roamex/common/roamex_prefs.h"

namespace roamex {

TabStripPlacement GetTabStripPlacement(const PrefService* pref_service) {
  if (!pref_service ||
      !base::FeatureList::IsEnabled(features::kTabStripPosition)) {
    return TabStripPlacement::kTop;
  }
  const int stored = pref_service->GetInteger(prefs::kTabStripPosition);
  if (stored < static_cast<int>(TabStripPlacement::kTop) ||
      stored > static_cast<int>(TabStripPlacement::kRight)) {
    return TabStripPlacement::kTop;
  }
  return static_cast<TabStripPlacement>(stored);
}

void SetTabStripPlacement(PrefService* pref_service,
                          TabStripPlacement placement) {
  if (!pref_service) {
    return;
  }
  pref_service->SetInteger(prefs::kTabStripPosition,
                           static_cast<int>(placement));
}

bool ShouldDisplayVerticalTabsForPlacement(const PrefService* pref_service) {
  const TabStripPlacement placement = GetTabStripPlacement(pref_service);
  return placement == TabStripPlacement::kLeft ||
         placement == TabStripPlacement::kRight;
}

bool ShouldDockVerticalTabStripRight(const PrefService* pref_service) {
  if (GetTabStripPlacement(pref_service) != TabStripPlacement::kRight) {
    return false;
  }
  // Robust to contexts where the upstream pref is not registered.
  const PrefService::Preference* upstream =
      pref_service->FindPreference(kUpstreamVerticalTabsEnabledPref);
  const bool upstream_on =
      upstream && upstream->GetValue()->GetIfBool().value_or(false);
  return !upstream_on;
}

bool ShouldDockVerticalTabStripLeft(const PrefService* pref_service) {
  if (GetTabStripPlacement(pref_service) != TabStripPlacement::kLeft) {
    return false;
  }
  const PrefService::Preference* upstream =
      pref_service->FindPreference(kUpstreamVerticalTabsEnabledPref);
  const bool upstream_on =
      upstream && upstream->GetValue()->GetIfBool().value_or(false);
  return !upstream_on;
}

BottomStripLayout ComputeBottomStripLayout(const gfx::Rect& client_area,
                                           int strip_height) {
  const int height = std::clamp(strip_height, 0, client_area.height());
  BottomStripLayout result;
  result.strip = gfx::Rect(client_area.x(), client_area.bottom() - height,
                           client_area.width(), height);
  result.remaining =
      gfx::Rect(client_area.x(), client_area.y(), client_area.width(),
                client_area.height() - height);
  return result;
}

}  // namespace roamex

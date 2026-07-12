// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_COMMON_TAB_STRIP_PLACEMENT_H_
#define ROAMUX_COMMON_TAB_STRIP_PLACEMENT_H_

#include "ui/gfx/geometry/rect.h"

class PrefService;

namespace roamux {

// Where the tab strip is docked (E1, flag roamux::features::kTabStripPosition).
// The numeric values are the persisted integers of prefs::kTabStripPosition —
// the forward contract I-1.2/I-1.3/I-1.4 consume; never renumber (stored user
// state depends on it).
//
// Placements name PHYSICAL window edges and are RTL-invariant (roam-9 D1):
// kLeft is the visual left in every UI direction, matching the upstream
// vertical strip's physical-left dock at this pin and the literal UI labels.
enum class TabStripPlacement {
  kTop = 0,  // Chromium default (the registered pref default).
  kBottom = 1,
  kLeft = 2,
  kRight = 3,
};

// The effective placement. kTop when the feature flag is off, `pref_service` is
// null, or the stored value is out of range (prefs are user-editable on disk) —
// consumers can never half-honor a placement while the epic flag is off.
TabStripPlacement GetTabStripPlacement(const PrefService* pref_service);

// Persists `placement`. No-op when `pref_service` is null.
void SetTabStripPlacement(PrefService* pref_service,
                          TabStripPlacement placement);

// The bottom-docked strip band (roam-7 / I-1.2): `strip` is the band carved
// off the bottom of `client_area`; `remaining` is everything above it.
struct BottomStripLayout {
  gfx::Rect strip;
  gfx::Rect remaining;
};

// Splits `client_area` into a bottom band of `strip_height` (clamped to
// [0, client_area.height()]) and the remaining area above it.
BottomStripLayout ComputeBottomStripLayout(const gfx::Rect& client_area,
                                           int strip_height);

// Mirror of upstream prefs::kVerticalTabsEnabled (chrome/common/pref_names.h) —
// //roamux/common cannot depend on //chrome. Roamux READS this pref, never
// writes it.
inline constexpr char kUpstreamVerticalTabsEnabledPref[] =
    "vertical_tabs.enabled";

// roam-8 (I-1.3): true when the roamux placement asks for a vertical strip
// (kLeft/kRight, flag on) — OR-ed into the upstream display gate by patch 0008.
bool ShouldDisplayVerticalTabsForPlacement(const PrefService* pref_service);

// roam-8 (I-1.3): true when the vertical strip should dock at the RIGHT edge —
// only when roamux-driven (placement kRight) and the upstream pref is not
// explicitly on (the upstream pref keeps today's left dock).
bool ShouldDockVerticalTabStripRight(const PrefService* pref_service);

// roam-9 (I-1.4): true when the vertical strip is roamux-driven onto the
// physical LEFT edge (placement kLeft, upstream pref off). Placements are
// physical and RTL-invariant (D1); layout code flips to logical coordinates
// at the call site.
bool ShouldDockVerticalTabStripLeft(const PrefService* pref_service);

}  // namespace roamux

#endif  // ROAMUX_COMMON_TAB_STRIP_PLACEMENT_H_

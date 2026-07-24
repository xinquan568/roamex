// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_BROWSER_UI_VIEWS_TABS_VERTICAL_TAB_STRIP_ROAMUX_OBSERVER_H_
#define ROAMUX_BROWSER_UI_VIEWS_TABS_VERTICAL_TAB_STRIP_ROAMUX_OBSERVER_H_

namespace roamux {

// roam-214: the notify-only seam the upstream VerticalTabStripRegionView
// reports through (patch 0054). All logic lives on the Roamux side; the
// upstream hunks are single guarded calls.
class VerticalTabStripRoamuxObserver {
 public:
  // A genuine pointer exit from the region view (post-debounce on Windows).
  virtual void OnRoamuxPointerExit() = 0;

  // Focus moved into or out of the region view.
  virtual void OnRoamuxFocusChanged() = 0;

  // A keep-open-relevant lock change. `total_keep_open_count` is the
  // POST-change keep_current + keep_expanded total (INCLUDING Roamux's own
  // locks — the glue subtracts its own); fired BEFORE the upstream
  // hover-state recompute so an armed pin can dissolve instead of
  // re-expanding (D9 omnibox ordering).
  virtual void OnRoamuxKeepOpenLocksChanged(int total_keep_open_count,
                                            bool released) = 0;

  // A context-menu command opened from the strip actually EXECUTED
  // (dismissal without choosing never reports — D3).
  virtual void OnRoamuxStripMenuCommandExecuted() = 0;

  // A strip-owned control (e.g. the new-tab button) completed an activation
  // — the non-tab keyboard/mouse completion signal (D3).
  virtual void OnRoamuxStripControlActivated() = 0;

 protected:
  virtual ~VerticalTabStripRoamuxObserver() = default;
};

}  // namespace roamux

#endif  // ROAMUX_BROWSER_UI_VIEWS_TABS_VERTICAL_TAB_STRIP_ROAMUX_OBSERVER_H_

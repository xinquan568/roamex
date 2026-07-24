// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_BROWSER_UI_VIEWS_TABS_TAB_STRIP_TOGGLE_COMMAND_H_
#define ROAMUX_BROWSER_UI_VIEWS_TABS_TAB_STRIP_TOGGLE_COMMAND_H_

#include "base/functional/callback.h"

class Browser;
class BrowserWindowInterface;
class VerticalTabStripRegionView;

namespace roamux::tabs_toggle {

// roam-214: the always-built command shim (plan F5 resolution). Patch 0053
// wires IDC_ROAMUX_TOGGLE_TAB_STRIP into BrowserCommandController against
// these functions; the per-window pin controller glue (patch 0054) installs
// the real handler at construction. With no handler installed (0053-only
// builds, or flag-off windows where the controller is never created) the
// command executes as a deliberate no-op.

// D10 availability: feature flag on AND placement in {left,right} AND the
// upstream vertical-tabs feature active. Also consulted by the macOS
// registry resolver's availability filter (plan R1), so key-event
// resolution and command enablement can never disagree.
bool CanToggleTabStrip(BrowserWindowInterface* browser);

// Executes the toggle for `browser` via the installed handler; no-op
// without one.
void ToggleTabStrip(BrowserWindowInterface* browser);

// Handler registration (patch-0054 glue; per-window lifetime).
void SetToggleHandler(BrowserWindowInterface* browser,
                      base::RepeatingClosure handler);
void ClearToggleHandler(BrowserWindowInterface* browser);

// The notify-only signal hooks the patched upstream seams (patch 0054) fire
// into. ALWAYS BUILT (this TU compiles under patch 0053 alone), so 0053-only
// builds link and every signal is a no-op until the pin-controller glue
// (compiled via 0054) registers its hooks.
struct RoamuxTabStripSignalHooks {
  RoamuxTabStripSignalHooks();
  RoamuxTabStripSignalHooks(RoamuxTabStripSignalHooks&&);
  RoamuxTabStripSignalHooks& operator=(RoamuxTabStripSignalHooks&&);
  ~RoamuxTabStripSignalHooks();

  base::RepeatingClosure user_activation;  // tab press / handled Enter
  base::RepeatingClosure menu_executed;    // strip menu command executed
  base::RepeatingCallback<void(VerticalTabStripRegionView*)> region_created;
  base::RepeatingCallback<void(VerticalTabStripRegionView*)> region_destroyed;
};
void SetSignalHooks(Browser* browser, RoamuxTabStripSignalHooks hooks);
void ClearSignalHooks(Browser* browser);

}  // namespace roamux::tabs_toggle

namespace roamux {

// Free functions the patched upstream seams call (single-line hunks).
// No-ops when no hooks are installed for the browser (flag off, or
// 0053-only builds).
void OnVerticalTabStripRegionViewCreated(
    Browser* browser,
    VerticalTabStripRegionView* region_view);
void OnVerticalTabStripRegionViewDestroyed(
    Browser* browser,
    VerticalTabStripRegionView* region_view);
void OnVerticalTabStripMenuCommandExecuted(Browser* browser);
void OnVerticalTabStripUserActivation(const BrowserWindowInterface* browser);

}  // namespace roamux

#endif  // ROAMUX_BROWSER_UI_VIEWS_TABS_TAB_STRIP_TOGGLE_COMMAND_H_

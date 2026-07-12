// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_BROWSER_TAB_VISIT_TAB_VISIT_COMMAND_H_
#define ROAMUX_BROWSER_TAB_VISIT_TAB_VISIT_COMMAND_H_

#include <string>

class Browser;
class BrowserWindowInterface;

namespace roamux::tab_visit {

// Behavior for IDC_TAB_VISIT_BACK / IDC_TAB_VISIT_FORWARD (roam-25 / I-4.5),
// wired from BrowserCommandController via patch (mirrors roam-12's
// reload_initial_url_command). Each is one STEP of the traversal gesture — it
// forwards to the per-profile TabVisitTraversalCoordinator, which begins the
// gesture on the first step and settles on modifier-release / debounce.
//
// These entry points also LAZILY inject the reopen action (roam-27) into the
// per-profile coordinator (the coordinator lives in //chrome/browser/ui/tabs
// and cannot reach TabRestoreService / LiveTabContext without a GN cycle, so
// the reopen shim below is injected from this ui-layer target).
bool CanTabVisitBack(Browser* browser);
bool CanTabVisitForward(Browser* browser);
void TabVisitBack(Browser* browser);
void TabVisitForward(Browser* browser);

// roam-27 (I-4.7): the reopen shim injected into the coordinator's `ReopenFn`.
// Reopens the closed tab with durable `uid` into `target`'s window: the EXACT
// tab via TabRestoreService (matching the uid stamped in the closed entry's
// extra_data — roam-10 patch 0009), else an eviction fallback that opens a
// fresh tab at `last_known_url` re-stamped with the old uid. Returns true if a
// tab was (re)opened. Lives in //chrome/browser/ui (needs Browser-layer types).
bool ReopenClosedTabByUid(BrowserWindowInterface* target,
                          const std::string& uid,
                          const std::string& last_known_url);

}  // namespace roamux::tab_visit

#endif  // ROAMUX_BROWSER_TAB_VISIT_TAB_VISIT_COMMAND_H_

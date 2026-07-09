// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMEX_BROWSER_TAB_VISIT_TAB_VISIT_COMMAND_H_
#define ROAMEX_BROWSER_TAB_VISIT_TAB_VISIT_COMMAND_H_

class Browser;

namespace roamex::tab_visit {

// Behavior for IDC_TAB_VISIT_BACK / IDC_TAB_VISIT_FORWARD (roam-25 / I-4.5),
// wired from BrowserCommandController via patch (mirrors roam-12's
// reload_initial_url_command). Each is one STEP of the traversal gesture — it
// forwards to the per-profile TabVisitTraversalCoordinator, which begins the
// gesture on the first step and settles on modifier-release / debounce.
bool CanTabVisitBack(Browser* browser);
bool CanTabVisitForward(Browser* browser);
void TabVisitBack(Browser* browser);
void TabVisitForward(Browser* browser);

}  // namespace roamex::tab_visit

#endif  // ROAMEX_BROWSER_TAB_VISIT_TAB_VISIT_COMMAND_H_

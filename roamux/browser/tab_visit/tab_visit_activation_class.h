// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_BROWSER_TAB_VISIT_TAB_VISIT_ACTIVATION_CLASS_H_
#define ROAMUX_BROWSER_TAB_VISIT_TAB_VISIT_ACTIVATION_CLASS_H_

namespace roamux::tab_visit {

// Mirrors chrome's `TabStripModelChange::Type` WITHOUT depending on
// //chrome/browser/ui, so the commit decision below stays unit-testable inside
// the pure `//roamux/browser/tab_visit` source_set. The observer bridge (which
// lives in the UI target) maps `change.type()` onto this enum.
enum class TabActivationChange {
  kSelectionOnly,
  kInserted,
  kRemoved,
  kMoved,
  kReplaced,
};

// The commit rule (roam-23 / I-4.3). A settled visit is committed ONLY for a
// genuine selection of an already-existing tab — a `kSelectionOnly` change that
// actually moved the active tab. Every other class is suppressed:
//   * kRemoved   — the auto-successor selected when the active tab is closed,
//                  plus tear-off and window-close removals (the HARD no-commit
//                  requirement);
//   * kReplaced  — a discard/replacement (the tab did not change, its contents
//                  did);
//   * kInserted  — a new foreground tab (its URL is the NTP; a later real
//                  navigation in it is not an activation);
//   * kMoved     — a reorder.
//
// The rule keys on the change TYPE, not the gesture reason, so an API
// activation (`chrome.tabs.update({active:true})` -> `ActivateTabAt(kNone)` ->
// CHANGE_REASON_NONE) is still a `kSelectionOnly` selection and therefore
// commits, while the auto-successor (also reason NONE, but `kRemoved`) does
// not.
constexpr bool ShouldCommitActivation(TabActivationChange type,
                                      bool active_tab_changed) {
  return active_tab_changed && type == TabActivationChange::kSelectionOnly;
}

}  // namespace roamux::tab_visit

#endif  // ROAMUX_BROWSER_TAB_VISIT_TAB_VISIT_ACTIVATION_CLASS_H_

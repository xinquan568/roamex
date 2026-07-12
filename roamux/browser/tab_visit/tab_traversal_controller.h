// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_BROWSER_TAB_VISIT_TAB_TRAVERSAL_CONTROLLER_H_
#define ROAMUX_BROWSER_TAB_VISIT_TAB_TRAVERSAL_CONTROLLER_H_

#include <optional>
#include <string>
#include <vector>

#include "base/containers/flat_set.h"

namespace roamux::tab_visit {

// A durable, per-tab SEMANTIC identity (a tabUid-equivalent) — NOT a URL. Two
// tabs are distinct even if they share a URL; one tab keeps its identity across
// revisits. Kept as an opaque string so this pure controller has no storage
// dependency; the durable identity SOURCE (RoamuxTabDurableId / §13.2) and the
// journal->identity mapping are integration concerns (I-4.5/I-4.6). Integration
// gate: no user-visible Back/Forward path may key on a URL — a durable per-tab
// identity must be supplied before I-4.5 enables the commands.
using TabIdentity = std::string;

// Derived MRU (§6.2): scan `journal_oldest_first` newest->oldest and take the
// first occurrence of each distinct identity. Returns the distinct identities
// newest-first; the journal tail's identity is result[0].
std::vector<TabIdentity> DeriveMruOrder(
    const std::vector<TabIdentity>& journal_oldest_first);

// Pre-gesture Back enablement (§6.3): true iff a first Back would land — i.e.
// the derived MRU holds a reachable identity OLDER than the tail (index 0, the
// anchor origin, is excluded). Pre-gesture Forward is always false (the tail is
// already the most-recent tab), so no helper is provided for it.
bool CanBeginBackTraversal(const std::vector<TabIdentity>& journal_oldest_first,
                           const base::flat_set<TabIdentity>& reachable);

// The frozen-gesture derived-MRU walk (§6.3, plan lines 239-244). Back/Forward
// step over an MRU order captured ONCE at gesture start, so Forward exactly
// inverts Back. The anchor (journal tail = MRU index 0) is the positioning
// ORIGIN even when unreachable (a closed active tab): index 0 is skipped as a
// landing, so the first Back after an active-tab close lands on the
// MRU-previous reachable tab and is never a no-op. This class is pure logic (no
// Profile, no UI, no storage); the caller feeds it durable tab identities + a
// reachable set, invokes it only when `kTabVisitNav` is enabled, and appends
// the settled tab.
class TabTraversalController {
 public:
  TabTraversalController();
  ~TabTraversalController();
  TabTraversalController(const TabTraversalController&) = delete;
  TabTraversalController& operator=(const TabTraversalController&) = delete;

  // Start a gesture: frozenOrder = DeriveMruOrder(journal); an index is
  // landable iff its identity is in `reachable`; cursor = 0 (the anchor
  // origin). NO-OP if a gesture is already active — the caller must Settle()
  // first, which makes "frozen for the gesture" structural (a stray re-entry
  // cannot recompute the order or reset the cursor mid-walk).
  void BeginGesture(const std::vector<TabIdentity>& journal_oldest_first,
                    const base::flat_set<TabIdentity>& reachable);

  bool gesture_active() const { return active_; }

  // Enablement (§6.3): a landable index exists after / before the cursor.
  bool CanGoBack() const;
  bool CanGoForward() const;

  // Step to the next/previous landable index (Back = toward older, Forward =
  // toward newer); returns the identity landed on, or nullopt when disabled (a
  // no-op at the landable ends).
  std::optional<TabIdentity> Back();
  std::optional<TabIdentity> Forward();

  // The identity the cursor currently rests on, or nullopt if the cursor is on
  // the non-landable anchor origin.
  std::optional<TabIdentity> Current() const;

  // End the gesture; returns the settled landable identity to commit (== the
  // current landing) or nullopt if none (cursor still on a non-landable
  // origin). Clears all gesture state; the next gesture recomputes from a fresh
  // journal.
  std::optional<TabIdentity> Settle();

 private:
  // Smallest j > index with landable_[j], or -1. Largest j < index with
  // landable_[j], or -1.
  int NextLandableAfter(int index) const;
  int PrevLandableBefore(int index) const;

  std::vector<TabIdentity> frozen_order_;  // MRU order, anchor at index 0.
  std::vector<bool> landable_;             // Parallel to frozen_order_.
  int cursor_ = 0;
  bool active_ = false;
};

}  // namespace roamux::tab_visit

#endif  // ROAMUX_BROWSER_TAB_VISIT_TAB_TRAVERSAL_CONTROLLER_H_

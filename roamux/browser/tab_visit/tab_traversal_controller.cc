// SPDX-License-Identifier: Apache-2.0
#include "roamux/browser/tab_visit/tab_traversal_controller.h"

#include <cstddef>

namespace roamux::tab_visit {

std::vector<TabIdentity> DeriveMruOrder(
    const std::vector<TabIdentity>& journal_oldest_first) {
  std::vector<TabIdentity> mru;
  base::flat_set<TabIdentity> seen;
  // Newest -> oldest: the first time we see an identity is its MRU position.
  for (auto it = journal_oldest_first.rbegin();
       it != journal_oldest_first.rend(); ++it) {
    if (seen.insert(*it).second) {
      mru.push_back(*it);
    }
  }
  return mru;
}

bool CanBeginBackTraversal(const std::vector<TabIdentity>& journal_oldest_first,
                           const base::flat_set<TabIdentity>& reachable) {
  const std::vector<TabIdentity> mru = DeriveMruOrder(journal_oldest_first);
  // Index 0 is the anchor origin; a first Back must land on something OLDER.
  for (std::size_t i = 1; i < mru.size(); ++i) {
    if (reachable.contains(mru[i])) {
      return true;
    }
  }
  return false;
}

TabTraversalController::TabTraversalController() = default;
TabTraversalController::~TabTraversalController() = default;

void TabTraversalController::BeginGesture(
    const std::vector<TabIdentity>& journal_oldest_first,
    const base::flat_set<TabIdentity>& reachable) {
  // Frozen-for-the-gesture is structural: ignore re-entry while active. The
  // caller must Settle() before starting a new gesture, so a stray re-entry
  // cannot recompute the order or reset the cursor mid-walk.
  if (active_) {
    return;
  }
  frozen_order_ = DeriveMruOrder(journal_oldest_first);
  landable_.assign(frozen_order_.size(), false);
  for (std::size_t i = 0; i < frozen_order_.size(); ++i) {
    landable_[i] = reachable.contains(frozen_order_[i]);
  }
  cursor_ = 0;
  active_ = true;
}

int TabTraversalController::NextLandableAfter(int index) const {
  for (int j = index + 1; j < static_cast<int>(landable_.size()); ++j) {
    if (landable_[j]) {
      return j;
    }
  }
  return -1;
}

int TabTraversalController::PrevLandableBefore(int index) const {
  for (int j = index - 1; j >= 0; --j) {
    if (landable_[j]) {
      return j;
    }
  }
  return -1;
}

bool TabTraversalController::CanGoBack() const {
  return NextLandableAfter(cursor_) != -1;
}

bool TabTraversalController::CanGoForward() const {
  return PrevLandableBefore(cursor_) != -1;
}

std::optional<TabIdentity> TabTraversalController::Back() {
  const int j = NextLandableAfter(cursor_);
  if (j == -1) {
    return std::nullopt;
  }
  cursor_ = j;
  return frozen_order_[j];
}

std::optional<TabIdentity> TabTraversalController::Forward() {
  const int j = PrevLandableBefore(cursor_);
  if (j == -1) {
    return std::nullopt;
  }
  cursor_ = j;
  return frozen_order_[j];
}

std::optional<TabIdentity> TabTraversalController::Current() const {
  if (frozen_order_.empty() || cursor_ < 0 ||
      cursor_ >= static_cast<int>(frozen_order_.size()) ||
      !landable_[cursor_]) {
    return std::nullopt;
  }
  return frozen_order_[cursor_];
}

std::optional<TabIdentity> TabTraversalController::Settle() {
  std::optional<TabIdentity> settled = Current();
  frozen_order_.clear();
  landable_.clear();
  cursor_ = 0;
  active_ = false;
  return settled;
}

}  // namespace roamux::tab_visit

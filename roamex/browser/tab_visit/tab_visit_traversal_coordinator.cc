// SPDX-License-Identifier: Apache-2.0
#include "roamex/browser/tab_visit/tab_visit_traversal_coordinator.h"

#include <utility>

#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_user_gesture_details.h"
#include "content/public/browser/web_contents.h"
#include "roamex/browser/tab_visit/settled_visit_journal_factory.h"
#include "roamex/browser/tab_visit/settled_visit_journal_service.h"
#include "roamex/browser/tab_visit/visit_url_filter.h"
#include "roamex/browser/tabs/tab_uid_tab_helper.h"
#include "roamex/common/roamex_features.h"
#include "ui/base/base_window.h"
#include "url/gurl.h"

namespace roamex::tab_visit {

namespace {
// Fallback settle delay when a modifier release is never observed (§6.7 —
// release-primary, debounce fallback). Each step pushes the deadline out.
constexpr base::TimeDelta kSettleDebounce = base::Milliseconds(900);
}  // namespace

TabVisitTraversalCoordinator::TabVisitTraversalCoordinator(Profile* profile)
    : profile_(profile) {}

TabVisitTraversalCoordinator::~TabVisitTraversalCoordinator() = default;

// static
std::string TabVisitTraversalCoordinator::UidOf(
    content::WebContents* web_contents) {
  if (!web_contents) {
    return std::string();
  }
  tabs::TabUidTabHelper* helper =
      tabs::TabUidTabHelper::FromWebContents(web_contents);
  return helper ? helper->uid() : std::string();
}

base::flat_set<std::string> TabVisitTraversalCoordinator::CollectLiveUids()
    const {
  base::flat_set<std::string> uids;
  for (BrowserWindowInterface* browser : GetAllBrowserWindowInterfaces()) {
    if (browser->GetProfile() != profile_) {
      continue;
    }
    TabStripModel* model = browser->GetTabStripModel();
    for (int i = 0; i < model->count(); ++i) {
      std::string uid = UidOf(model->GetWebContentsAt(i));
      if (!uid.empty()) {
        uids.insert(std::move(uid));
      }
    }
  }
  return uids;
}

std::optional<TabVisitTraversalCoordinator::TabLocation>
TabVisitTraversalCoordinator::ResolveUid(const std::string& uid) const {
  if (uid.empty()) {
    return std::nullopt;
  }
  for (BrowserWindowInterface* browser : GetAllBrowserWindowInterfaces()) {
    if (browser->GetProfile() != profile_) {
      continue;
    }
    TabStripModel* model = browser->GetTabStripModel();
    for (int i = 0; i < model->count(); ++i) {
      content::WebContents* wc = model->GetWebContentsAt(i);
      if (UidOf(wc) == uid) {
        return TabLocation{browser, wc, i};
      }
    }
  }
  return std::nullopt;
}

void TabVisitTraversalCoordinator::RecordSettledUid(const std::string& uid) {
  if (uid.empty()) {
    return;
  }
  // Coalesce by uid: no consecutive duplicate tail (§6.3).
  if (!settled_uid_log_.empty() && settled_uid_log_.back() == uid) {
    return;
  }
  settled_uid_log_.push_back(uid);
}

void TabVisitTraversalCoordinator::EnsureGesture() {
  if (traversal_active_) {
    return;
  }
  anchor_uid_ =
      settled_uid_log_.empty() ? std::string() : settled_uid_log_.back();
  controller_.BeginGesture(settled_uid_log_, CollectLiveUids());
  traversal_active_ = true;
}

void TabVisitTraversalCoordinator::ActivateLocation(const TabLocation& loc) {
  loc.browser->GetTabStripModel()->ActivateTabAt(
      loc.index, TabStripUserGestureDetails(
                     TabStripUserGestureDetails::GestureType::kNone));
  // Focus the owning window (the landing may live in another window, §6.5).
  loc.browser->GetWindow()->Activate();
}

void TabVisitTraversalCoordinator::StepAndActivate(bool forward) {
  std::optional<TabIdentity> uid =
      forward ? controller_.Forward() : controller_.Back();
  if (uid) {
    if (std::optional<TabLocation> loc = ResolveUid(*uid)) {
      ActivateLocation(*loc);
    }
  }
  // (Re)arm the debounce fallback; each step pushes the settle deadline out.
  debounce_timer_.Start(FROM_HERE, kSettleDebounce,
                        base::BindOnce(&TabVisitTraversalCoordinator::Settle,
                                       base::Unretained(this)));
}

void TabVisitTraversalCoordinator::StepBack() {
  if (!base::FeatureList::IsEnabled(features::kTabVisitNav)) {
    return;
  }
  // Ignore a step that cannot land — do NOT start (or extend) a gesture on a
  // no-op direction (guards against stale command enablement, F1).
  if (!CanGoBack()) {
    return;
  }
  EnsureGesture();
  StepAndActivate(/*forward=*/false);
}

void TabVisitTraversalCoordinator::StepForward() {
  if (!base::FeatureList::IsEnabled(features::kTabVisitNav)) {
    return;
  }
  if (!CanGoForward()) {
    return;
  }
  EnsureGesture();
  StepAndActivate(/*forward=*/true);
}

void TabVisitTraversalCoordinator::Settle() {
  if (!traversal_active_) {
    return;
  }
  debounce_timer_.Stop();
  // Capture the settled uid; the pure controller clears its state here, but
  // `traversal_active_` STAYS true so the bridge remains suppressed across the
  // activation below (no double append).
  std::optional<TabIdentity> uid = controller_.Settle();
  // Commit exactly ONE settled visit — but only if the gesture actually landed
  // on a DIFFERENT tab than the anchor it started from. Returning to (or never
  // moving off) the anchor is not a new visit, so it must not re-append the
  // current tab (F1). A landed-but-now-closed tab commits nothing.
  if (uid && *uid != anchor_uid_) {
    if (std::optional<TabLocation> loc = ResolveUid(*uid)) {
      ActivateLocation(*loc);
      RecordSettledUid(*uid);
      const GURL& url = loc->web_contents->GetLastCommittedURL();
      if (IsRecordableVisitUrl(url)) {
        if (SettledVisitJournalService* journal =
                SettledVisitJournalFactory::GetForProfile(profile_)) {
          journal->RecordVisit(url);  // Exactly one append for the gesture.
        }
      }
    }
  }
  // Clear LAST — after the settle activation has been suppressed.
  traversal_active_ = false;
}

void TabVisitTraversalCoordinator::CancelGesture() {
  debounce_timer_.Stop();
  if (traversal_active_) {
    controller_.Settle();  // Drop the pure state without committing.
    traversal_active_ = false;
  }
}

bool TabVisitTraversalCoordinator::CanGoBack() const {
  if (traversal_active_) {
    return controller_.CanGoBack();
  }
  return CanBeginBackTraversal(settled_uid_log_, CollectLiveUids());
}

bool TabVisitTraversalCoordinator::CanGoForward() const {
  // Before a gesture the tail is already the most-recent tab (nothing newer).
  return traversal_active_ && controller_.CanGoForward();
}

void TabVisitTraversalCoordinator::Shutdown() {
  CancelGesture();
}

}  // namespace roamex::tab_visit

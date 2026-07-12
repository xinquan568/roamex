// SPDX-License-Identifier: Apache-2.0
#include "roamux/browser/tab_visit/tab_visit_observer_bridge.h"

#include <memory>
#include <string>

#include "base/feature_list.h"
#include "base/notreached.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tab_list/tab_removed_reason.h"
#include "chrome/browser/ui/browser_tab_strip_tracker.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/web_contents.h"
#include "roamux/browser/tab_visit/settled_visit_journal_factory.h"
#include "roamux/browser/tab_visit/settled_visit_journal_service.h"
#include "roamux/browser/tab_visit/tab_visit_activation_class.h"
#include "roamux/browser/tab_visit/tab_visit_traversal_coordinator.h"
#include "roamux/browser/tab_visit/tab_visit_traversal_coordinator_factory.h"
#include "roamux/browser/tab_visit/visit_url_filter.h"
#include "roamux/browser/tabs/tab_uid_tab_helper.h"
#include "roamux/common/roamux_features.h"
#include "url/gurl.h"

namespace roamux::tab_visit {

namespace {

// Maps chrome's TabStripModelChange::Type onto the pure, UI-free roamux mirror
// so the commit decision stays in the unit-testable predicate.
TabActivationChange ToActivationChange(TabStripModelChange::Type type) {
  switch (type) {
    case TabStripModelChange::kSelectionOnly:
      return TabActivationChange::kSelectionOnly;
    case TabStripModelChange::kInserted:
      return TabActivationChange::kInserted;
    case TabStripModelChange::kRemoved:
      return TabActivationChange::kRemoved;
    case TabStripModelChange::kMoved:
      return TabActivationChange::kMoved;
    case TabStripModelChange::kReplaced:
      return TabActivationChange::kReplaced;
  }
  NOTREACHED();
}

// roam-26: persist a `tab_state` closed row for every tab in a `kRemoved`
// change that is really being DELETED (a close), keyed by its durable uid, so
// it survives a restart. Moves (tear-off / side-panel) keep the tab live and
// are excluded. uid + URL are read synchronously here, while the removed
// contents are still valid in the observer callback.
void PersistClosedTabsToSidecar(const TabStripModelChange& change,
                                SettledVisitJournalService* journal,
                                TabVisitTraversalCoordinator* coordinator) {
  const TabStripModelChange::Remove* remove = change.GetRemove();
  if (!remove) {
    return;
  }
  for (const TabStripModelChange::RemovedTab& removed : remove->contents) {
    const bool will_delete =
        removed.remove_reason == TabRemovedReason::kDeleted ||
        removed.remove_reason == TabRemovedReason::kDeletedAndExpandSidePanel;
    if (!will_delete) {
      continue;
    }
    content::WebContents* contents = removed.contents;
    tabs::TabUidTabHelper* helper =
        contents ? tabs::TabUidTabHelper::FromWebContents(contents) : nullptr;
    if (!helper || helper->uid().empty()) {
      continue;
    }
    TabStateRow row;
    row.restore_key = helper->uid();
    row.closed = true;
    row.window_id =
        sessions::SessionTabHelper::IdForWindowContainingTab(contents).id();
    row.last_known_url = contents->GetLastCommittedURL().spec();
    // roam-27: register the closed tab as reopenable SYNCHRONOUSLY (before the
    // async persist) so a gesture right after this close can reopen it.
    if (coordinator) {
      coordinator->AddReopenable(row);
    }
    journal->SetTabState(std::move(row));
  }
}

}  // namespace

TabVisitObserverBridge::TabVisitObserverBridge(Profile* profile)
    : profile_(profile) {
  // Inert when the feature is disabled, even though the factory is eager — a
  // disabled build observes nothing and commits nothing (anti-pattern rule 3).
  if (!base::FeatureList::IsEnabled(features::kTabVisitNav)) {
    return;
  }
  tracker_ = std::make_unique<BrowserTabStripTracker>(this, this);
  tracker_->Init();
}

TabVisitObserverBridge::~TabVisitObserverBridge() = default;

void TabVisitObserverBridge::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  SettledVisitJournalService* journal =
      SettledVisitJournalFactory::GetForProfile(profile_);
  TabVisitTraversalCoordinator* coordinator =
      TabVisitTraversalCoordinatorFactory::GetForProfile(profile_);

  // roam-26 (structural): on a REAL close, persist the tab's sidecar closed row
  // so its state survives a restart (retained-and-skipped). This runs BEFORE
  // the activation-commit path and is NOT gated by traversal suppression — a
  // close during an active gesture must still persist. Tear-off / side-panel
  // MOVE reasons keep the tab live, so they are excluded.
  if (journal && change.type() == TabStripModelChange::kRemoved) {
    PersistClosedTabsToSidecar(change, journal, coordinator);
  }

  // roam-26: a tab insert/removal changes the reachable-live-uid set, so
  // Back/Forward command enablement should be recomputed — important after a
  // restart, where restored tabs register their durable uids as they attach
  // (background inserts that no active-tab change would otherwise refresh).
  if (coordinator && (change.type() == TabStripModelChange::kInserted ||
                      change.type() == TabStripModelChange::kRemoved)) {
    coordinator->OnReachabilityMaybeChanged();
  }

  if (!ShouldCommitActivation(ToActivationChange(change.type()),
                              selection.active_tab_changed())) {
    return;
  }
  content::WebContents* new_contents = selection.new_contents;
  if (!new_contents) {
    return;
  }

  // roam-25: while a traversal gesture previews tabs, the coordinator owns the
  // single settle-commit — suppress the bridge's per-activation commit so a
  // gesture yields exactly one append (§6.3).
  if (coordinator && coordinator->IsTraversalActive()) {
    return;
  }

  // The settled tab's durable uid keys both the volatile MRU commit-log and the
  // persisted journal row (roam-26: RecordVisit is uid-keyed).
  std::string tab_uid;
  if (tabs::TabUidTabHelper* helper =
          tabs::TabUidTabHelper::FromWebContents(new_contents)) {
    tab_uid = helper->uid();
  }
  if (coordinator && !tab_uid.empty()) {
    coordinator->RecordSettledUid(tab_uid);
  }

  const GURL& url = new_contents->GetLastCommittedURL();
  if (!IsRecordableVisitUrl(url)) {
    return;
  }
  if (journal) {
    journal->RecordVisit(tab_uid, url);
  }
}

bool TabVisitObserverBridge::ShouldTrackBrowser(
    BrowserWindowInterface* browser) {
  // Observe only THIS profile's normal Browsers; collectively the per-profile
  // bridges cover "across all Browsers", and OTR keeps its in-memory journal.
  return browser->GetProfile() == profile_ &&
         browser->GetType() == BrowserWindowInterface::TYPE_NORMAL;
}

void TabVisitObserverBridge::Shutdown() {
  // Unregister the observer before the profile / journal tear down.
  tracker_.reset();
}

}  // namespace roamux::tab_visit

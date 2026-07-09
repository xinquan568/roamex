// SPDX-License-Identifier: Apache-2.0
#include "roamex/browser/tab_visit/tab_visit_observer_bridge.h"

#include <memory>

#include "base/feature_list.h"
#include "base/notreached.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_tab_strip_tracker.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "roamex/browser/tab_visit/settled_visit_journal_factory.h"
#include "roamex/browser/tab_visit/settled_visit_journal_service.h"
#include "roamex/browser/tab_visit/tab_visit_activation_class.h"
#include "roamex/browser/tab_visit/tab_visit_traversal_coordinator.h"
#include "roamex/browser/tab_visit/tab_visit_traversal_coordinator_factory.h"
#include "roamex/browser/tab_visit/visit_url_filter.h"
#include "roamex/browser/tabs/tab_uid_tab_helper.h"
#include "roamex/common/roamex_features.h"
#include "url/gurl.h"

namespace roamex::tab_visit {

namespace {

// Maps chrome's TabStripModelChange::Type onto the pure, UI-free roamex mirror
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
  TabVisitTraversalCoordinator* coordinator =
      TabVisitTraversalCoordinatorFactory::GetForProfile(profile_);
  if (coordinator && coordinator->IsTraversalActive()) {
    return;
  }

  // A normal settled activation contributes the tab's durable uid to the
  // traversal MRU commit-log (the ordering source, keyed by uid not URL).
  if (coordinator) {
    if (tabs::TabUidTabHelper* helper =
            tabs::TabUidTabHelper::FromWebContents(new_contents)) {
      coordinator->RecordSettledUid(helper->uid());
    }
  }

  const GURL& url = new_contents->GetLastCommittedURL();
  if (!IsRecordableVisitUrl(url)) {
    return;
  }
  if (SettledVisitJournalService* journal =
          SettledVisitJournalFactory::GetForProfile(profile_)) {
    journal->RecordVisit(url);
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

}  // namespace roamex::tab_visit

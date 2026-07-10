// SPDX-License-Identifier: Apache-2.0
#include "roamex/browser/tab_visit/settled_visit_journal_factory.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_context.h"
#include "roamex/browser/tab_visit/settled_visit_clear_hook.h"
#include "roamex/browser/tab_visit/settled_visit_journal_service.h"
#include "roamex/browser/tab_visit/tab_visit_traversal_coordinator.h"
#include "roamex/browser/tab_visit/tab_visit_traversal_coordinator_factory.h"

namespace roamex::tab_visit {

namespace {
// The per-profile journal database file, created under the profile directory.
constexpr base::FilePath::CharType kJournalDbName[] =
    FILE_PATH_LITERAL("RoamexTabVisits");

// After the async store clear completes, resync the coordinator's reopenable
// cache to the now-GC'd sidecar (weak-guarded — the profile stays alive during
// a browsing-data clear, but be safe), then run the completion token. The
// gesture cancel + in-memory URL purge already happened synchronously in
// PrepareForBrowsingDataClear before the clear was posted (roam-28 F1).
void ResyncCoordinatorThenDone(
    base::WeakPtr<TabVisitTraversalCoordinator> coordinator,
    base::OnceClosure done) {
  if (coordinator) {
    coordinator->OnBrowsingDataCleared();
  }
  std::move(done).Run();
}

// The ui-layer Clear-Browsing-Data hook impl (roam-28 / I-4.8), registered into
// the low //roamex/browser/tab_visit registry from the factory ctor. It runs on
// the UI thread when the BrowsingDataRemover delegate clears DATA_TYPE_HISTORY:
// clears the profile's journal visits in [begin, end) + orphaned closed
// tab_state, then purges the coordinator's in-memory reopenable cache, then
// runs `done`. Compiled into //chrome/browser/ui/tabs (patch 0015), where the
// per-profile factory + coordinator are reachable.
void ClearJournalForBrowsingData(content::BrowserContext* context,
                                 base::Time begin,
                                 base::Time end,
                                 base::OnceClosure done) {
  Profile* profile = Profile::FromBrowserContext(context);
  SettledVisitJournalService* journal =
      SettledVisitJournalFactory::GetForProfile(profile);
  if (!journal) {
    std::move(done).Run();  // Feature off / no service — token still fires.
    return;
  }
  TabVisitTraversalCoordinator* coordinator =
      TabVisitTraversalCoordinatorFactory::GetForProfile(profile);
  if (coordinator) {
    // SYNCHRONOUSLY, before posting the async store clear: cancel any in-flight
    // gesture + drop in-memory closed-tab URLs, so a Settle / new gesture in
    // the clear window cannot re-append or reopen a cleared URL (roam-28
    // F1/F2).
    coordinator->PrepareForBrowsingDataClear();
  }
  base::WeakPtr<TabVisitTraversalCoordinator> weak_coordinator =
      coordinator ? coordinator->AsWeakPtr()
                  : base::WeakPtr<TabVisitTraversalCoordinator>();
  journal->ClearBrowsingData(begin, end,
                             base::BindOnce(&ResyncCoordinatorThenDone,
                                            weak_coordinator, std::move(done)));
}
}  // namespace

// static
SettledVisitJournalService* SettledVisitJournalFactory::GetForProfile(
    Profile* profile) {
  return static_cast<SettledVisitJournalService*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

// static
SettledVisitJournalFactory* SettledVisitJournalFactory::GetInstance() {
  static base::NoDestructor<SettledVisitJournalFactory> instance;
  return instance.get();
}

SettledVisitJournalFactory::SettledVisitJournalFactory()
    : ProfileKeyedServiceFactory(
          "RoamexSettledVisitJournalService",
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kOwnInstance)
              .Build()) {
  // roam-28: register the Clear-Browsing-Data hook so the browsing_data
  // delegate (a lower GN layer) can clear this profile's journal without a GN
  // cycle. The factory singleton is built at startup by the profiles
  // registration, so the hook is set before any clear can run.
  SetClearJournalHook(base::BindRepeating(&ClearJournalForBrowsingData));
}

SettledVisitJournalFactory::~SettledVisitJournalFactory() = default;

std::unique_ptr<KeyedService>
SettledVisitJournalFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  const bool in_memory = profile->IsOffTheRecord();
  // OTR: no path (in-memory). Regular: the journal DB under the profile dir.
  base::FilePath db_path =
      in_memory ? base::FilePath() : profile->GetPath().Append(kJournalDbName);
  return std::make_unique<SettledVisitJournalService>(in_memory,
                                                      std::move(db_path));
}

}  // namespace roamex::tab_visit

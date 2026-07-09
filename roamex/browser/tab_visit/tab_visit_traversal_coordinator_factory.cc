// SPDX-License-Identifier: Apache-2.0
#include "roamex/browser/tab_visit/tab_visit_traversal_coordinator_factory.h"

#include <memory>

#include "chrome/browser/profiles/profile.h"
#include "roamex/browser/tab_visit/settled_visit_journal_factory.h"
#include "roamex/browser/tab_visit/tab_visit_traversal_coordinator.h"
#include "roamex/browser/tabs/tab_uid_service_factory.h"

namespace roamex::tab_visit {

// static
TabVisitTraversalCoordinator*
TabVisitTraversalCoordinatorFactory::GetForProfile(Profile* profile) {
  return static_cast<TabVisitTraversalCoordinator*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

// static
TabVisitTraversalCoordinatorFactory*
TabVisitTraversalCoordinatorFactory::GetInstance() {
  static base::NoDestructor<TabVisitTraversalCoordinatorFactory> instance;
  return instance.get();
}

TabVisitTraversalCoordinatorFactory::TabVisitTraversalCoordinatorFactory()
    : ProfileKeyedServiceFactory(
          "RoamexTabVisitTraversalCoordinator",
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kOwnInstance)
              .Build()) {
  DependsOn(SettledVisitJournalFactory::GetInstance());
  DependsOn(tabs::TabUidServiceFactory::GetInstance());
}

TabVisitTraversalCoordinatorFactory::~TabVisitTraversalCoordinatorFactory() =
    default;

std::unique_ptr<KeyedService>
TabVisitTraversalCoordinatorFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  return std::make_unique<TabVisitTraversalCoordinator>(
      Profile::FromBrowserContext(context));
}

bool TabVisitTraversalCoordinatorFactory::ServiceIsCreatedWithBrowserContext()
    const {
  return true;  // Eager: reachable by the bridge + watchers from session start.
}

}  // namespace roamex::tab_visit

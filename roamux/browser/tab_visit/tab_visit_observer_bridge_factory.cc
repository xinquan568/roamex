// SPDX-License-Identifier: Apache-2.0
#include "roamux/browser/tab_visit/tab_visit_observer_bridge_factory.h"

#include <memory>

#include "chrome/browser/profiles/profile.h"
#include "roamux/browser/tab_visit/settled_visit_journal_factory.h"
#include "roamux/browser/tab_visit/tab_visit_observer_bridge.h"

namespace roamux::tab_visit {

// static
TabVisitObserverBridge* TabVisitObserverBridgeFactory::GetForProfile(
    Profile* profile) {
  return static_cast<TabVisitObserverBridge*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

// static
TabVisitObserverBridgeFactory* TabVisitObserverBridgeFactory::GetInstance() {
  static base::NoDestructor<TabVisitObserverBridgeFactory> instance;
  return instance.get();
}

TabVisitObserverBridgeFactory::TabVisitObserverBridgeFactory()
    : ProfileKeyedServiceFactory(
          "RoamuxTabVisitObserverBridge",
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kOwnInstance)
              .Build()) {
  // The bridge commits into the settled-visit journal, so its service must
  // outlive the bridge and be built first.
  DependsOn(SettledVisitJournalFactory::GetInstance());
}

TabVisitObserverBridgeFactory::~TabVisitObserverBridgeFactory() = default;

std::unique_ptr<KeyedService>
TabVisitObserverBridgeFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  return std::make_unique<TabVisitObserverBridge>(
      Profile::FromBrowserContext(context));
}

bool TabVisitObserverBridgeFactory::ServiceIsCreatedWithBrowserContext() const {
  // Eager: start observing tab activations at profile init.
  return true;
}

}  // namespace roamux::tab_visit

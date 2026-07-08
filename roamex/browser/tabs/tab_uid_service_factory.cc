// SPDX-License-Identifier: Apache-2.0
#include "roamex/browser/tabs/tab_uid_service_factory.h"

#include "chrome/browser/profiles/profile.h"
#include "roamex/browser/tabs/tab_uid_service.h"

namespace roamex::tabs {

// static
TabUidService* TabUidServiceFactory::GetForProfile(Profile* profile) {
  return static_cast<TabUidService*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

// static
TabUidServiceFactory* TabUidServiceFactory::GetInstance() {
  static base::NoDestructor<TabUidServiceFactory> instance;
  return instance.get();
}

TabUidServiceFactory::TabUidServiceFactory()
    : ProfileKeyedServiceFactory(
          "RoamexTabUidService",
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kOwnInstance)
              .Build()) {}

TabUidServiceFactory::~TabUidServiceFactory() = default;

std::unique_ptr<KeyedService>
TabUidServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  return std::make_unique<TabUidService>();
}

}  // namespace roamex::tabs

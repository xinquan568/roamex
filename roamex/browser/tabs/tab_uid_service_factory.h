// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMEX_BROWSER_TABS_TAB_UID_SERVICE_FACTORY_H_
#define ROAMEX_BROWSER_TABS_TAB_UID_SERVICE_FACTORY_H_

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class Profile;

namespace roamex::tabs {

class TabUidService;

// Profile policy (roam-10 D5): regular profiles get their OWN service and OTR
// gets a SEPARATE in-memory service — never redirected to the original
// profile, so incognito tab identities never enter the regular registry.
// (This file is compiled into the upstream tab target via patch 0009's
// `sources +=`; it needs //chrome/browser/profiles.)
class TabUidServiceFactory : public ProfileKeyedServiceFactory {
 public:
  static TabUidService* GetForProfile(Profile* profile);
  static TabUidServiceFactory* GetInstance();

 private:
  friend class base::NoDestructor<TabUidServiceFactory>;
  TabUidServiceFactory();
  ~TabUidServiceFactory() override;

  // BrowserContextKeyedServiceFactory:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
};

}  // namespace roamex::tabs

#endif  // ROAMEX_BROWSER_TABS_TAB_UID_SERVICE_FACTORY_H_

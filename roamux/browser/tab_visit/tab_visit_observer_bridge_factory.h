// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_BROWSER_TAB_VISIT_TAB_VISIT_OBSERVER_BRIDGE_FACTORY_H_
#define ROAMUX_BROWSER_TAB_VISIT_TAB_VISIT_OBSERVER_BRIDGE_FACTORY_H_

#include <memory>

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class Profile;

namespace roamux::tab_visit {

class TabVisitObserverBridge;

// Eager per-profile factory for TabVisitObserverBridge (roam-23 / I-4.3). It
// mirrors the roam-21 journal factory's profile policy (own-instance regular +
// own-instance OTR, never redirected) so each profile's bridge writes to that
// same profile's journal, and overrides ServiceIsCreatedWithBrowserContext so
// the observer starts at profile init. It deliberately does NOT override
// ServiceIsNULLWhileTesting (unlike PinnedTabServiceFactory) so browsertests
// get a live bridge. This file is compiled into the upstream tab target via
// patch 0016 (it needs //chrome/browser/profiles).
class TabVisitObserverBridgeFactory : public ProfileKeyedServiceFactory {
 public:
  static TabVisitObserverBridge* GetForProfile(Profile* profile);
  static TabVisitObserverBridgeFactory* GetInstance();

 private:
  friend class base::NoDestructor<TabVisitObserverBridgeFactory>;
  TabVisitObserverBridgeFactory();
  ~TabVisitObserverBridgeFactory() override;

  // BrowserContextKeyedServiceFactory:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  bool ServiceIsCreatedWithBrowserContext() const override;
};

}  // namespace roamux::tab_visit

#endif  // ROAMUX_BROWSER_TAB_VISIT_TAB_VISIT_OBSERVER_BRIDGE_FACTORY_H_

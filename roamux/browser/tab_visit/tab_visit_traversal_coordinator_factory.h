// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_BROWSER_TAB_VISIT_TAB_VISIT_TRAVERSAL_COORDINATOR_FACTORY_H_
#define ROAMUX_BROWSER_TAB_VISIT_TAB_VISIT_TRAVERSAL_COORDINATOR_FACTORY_H_

#include <memory>

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class Profile;

namespace roamux::tab_visit {

class TabVisitTraversalCoordinator;

// Per-profile factory for TabVisitTraversalCoordinator (roam-25 / I-4.5). Eager
// (ServiceIsCreatedWithBrowserContext) so the roam-23 bridge and any window's
// gesture watcher can reach the same session; own-instance regular + OTR
// (mirrors the journal factory). Depends on the journal + tab-uid factories.
// Compiled into //chrome/browser/ui/tabs via a patch (needs
// //chrome/browser/ui).
class TabVisitTraversalCoordinatorFactory : public ProfileKeyedServiceFactory {
 public:
  static TabVisitTraversalCoordinator* GetForProfile(Profile* profile);
  static TabVisitTraversalCoordinatorFactory* GetInstance();

 private:
  friend class base::NoDestructor<TabVisitTraversalCoordinatorFactory>;
  TabVisitTraversalCoordinatorFactory();
  ~TabVisitTraversalCoordinatorFactory() override;

  // BrowserContextKeyedServiceFactory:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  bool ServiceIsCreatedWithBrowserContext() const override;
};

}  // namespace roamux::tab_visit

#endif  // ROAMUX_BROWSER_TAB_VISIT_TAB_VISIT_TRAVERSAL_COORDINATOR_FACTORY_H_

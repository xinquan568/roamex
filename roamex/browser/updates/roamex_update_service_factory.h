// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMEX_BROWSER_UPDATES_ROAMEX_UPDATE_SERVICE_FACTORY_H_
#define ROAMEX_BROWSER_UPDATES_ROAMEX_UPDATE_SERVICE_FACTORY_H_

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class Profile;

namespace roamex::updates {

class RoamexUpdateService;

// roam-85 (I-6.5): a per-profile facade over one process-wide Sparkle owner —
// updates are app-wide. Only regular profiles get a service; OTR/system get
// none (no update UI in incognito/system). Compiled into the upstream browser
// target via patch 0026's `sources +=` (needs //chrome/browser/profiles).
class RoamexUpdateServiceFactory : public ProfileKeyedServiceFactory {
 public:
  static RoamexUpdateService* GetForProfile(Profile* profile);
  static RoamexUpdateServiceFactory* GetInstance();

 private:
  friend class base::NoDestructor<RoamexUpdateServiceFactory>;
  RoamexUpdateServiceFactory();
  ~RoamexUpdateServiceFactory() override;

  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
};

}  // namespace roamex::updates

#endif  // ROAMEX_BROWSER_UPDATES_ROAMEX_UPDATE_SERVICE_FACTORY_H_

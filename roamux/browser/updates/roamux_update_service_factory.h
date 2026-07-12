// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_BROWSER_UPDATES_ROAMUX_UPDATE_SERVICE_FACTORY_H_
#define ROAMUX_BROWSER_UPDATES_ROAMUX_UPDATE_SERVICE_FACTORY_H_

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class Profile;

namespace roamux::updates {

class RoamuxUpdateService;

// roam-85 (I-6.5): a per-profile facade over one process-wide Sparkle owner —
// updates are app-wide. Only regular profiles get a service; OTR/system get
// none (no update UI in incognito/system). Compiled into the upstream browser
// target via patch 0026's `sources +=` (needs //chrome/browser/profiles).
class RoamuxUpdateServiceFactory : public ProfileKeyedServiceFactory {
 public:
  static RoamuxUpdateService* GetForProfile(Profile* profile);
  static RoamuxUpdateServiceFactory* GetInstance();

 private:
  friend class base::NoDestructor<RoamuxUpdateServiceFactory>;
  RoamuxUpdateServiceFactory();
  ~RoamuxUpdateServiceFactory() override;

  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
};

}  // namespace roamux::updates

#endif  // ROAMUX_BROWSER_UPDATES_ROAMUX_UPDATE_SERVICE_FACTORY_H_

// SPDX-License-Identifier: Apache-2.0
#include "roamux/browser/updates/roamux_update_service_factory.h"

#include <memory>

#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_context.h"
#include "roamux/browser/updates/roamux_update_service.h"

namespace roamux::updates {

// static
RoamuxUpdateService* RoamuxUpdateServiceFactory::GetForProfile(
    Profile* profile) {
  return static_cast<RoamuxUpdateService*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

// static
RoamuxUpdateServiceFactory* RoamuxUpdateServiceFactory::GetInstance() {
  static base::NoDestructor<RoamuxUpdateServiceFactory> instance;
  return instance.get();
}

RoamuxUpdateServiceFactory::RoamuxUpdateServiceFactory()
    : ProfileKeyedServiceFactory(
          "RoamuxUpdateService",
          // Regular (original) profiles only — no update UI in OTR/system, so
          // the branded settings/help update card degrades there
          // (updatesAvailable=false). roam-140: kOriginalOnly (NOT
          // kRedirectedToOriginal — that would hand OTR the original's service
          // and wrongly light up the update card off the record).
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kOriginalOnly)
              .WithGuest(ProfileSelection::kNone)
              .WithSystem(ProfileSelection::kNone)
              .Build()) {}

RoamuxUpdateServiceFactory::~RoamuxUpdateServiceFactory() = default;

std::unique_ptr<KeyedService>
RoamuxUpdateServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  return std::make_unique<RoamuxUpdateService>();
}

}  // namespace roamux::updates

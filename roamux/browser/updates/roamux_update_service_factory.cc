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
          // Regular profiles only — no update UI in OTR/system; the single
          // process-wide Sparkle owner backs every per-profile facade.
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kRedirectedToOriginal)
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

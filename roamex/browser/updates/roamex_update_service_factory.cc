// SPDX-License-Identifier: Apache-2.0
#include "roamex/browser/updates/roamex_update_service_factory.h"

#include <memory>

#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_context.h"
#include "roamex/browser/updates/roamex_update_service.h"

namespace roamex::updates {

// static
RoamexUpdateService* RoamexUpdateServiceFactory::GetForProfile(
    Profile* profile) {
  return static_cast<RoamexUpdateService*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

// static
RoamexUpdateServiceFactory* RoamexUpdateServiceFactory::GetInstance() {
  static base::NoDestructor<RoamexUpdateServiceFactory> instance;
  return instance.get();
}

RoamexUpdateServiceFactory::RoamexUpdateServiceFactory()
    : ProfileKeyedServiceFactory(
          "RoamexUpdateService",
          // Regular profiles only — no update UI in OTR/system; the single
          // process-wide Sparkle owner backs every per-profile facade.
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kRedirectedToOriginal)
              .WithGuest(ProfileSelection::kNone)
              .WithSystem(ProfileSelection::kNone)
              .Build()) {}

RoamexUpdateServiceFactory::~RoamexUpdateServiceFactory() = default;

std::unique_ptr<KeyedService>
RoamexUpdateServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  return std::make_unique<RoamexUpdateService>();
}

}  // namespace roamex::updates

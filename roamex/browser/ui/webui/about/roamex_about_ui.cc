// SPDX-License-Identifier: Apache-2.0
#include "roamex/browser/ui/webui/about/roamex_about_ui.h"

#include "base/containers/span.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/grit/roamex_about_resources.h"
#include "chrome/grit/roamex_about_resources_map.h"
#include "components/version_info/version_info.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "ui/webui/webui_util.h"

#if BUILDFLAG(ROAMEX_ENABLE_SPARKLE)
#include "roamex/browser/updates/roamex_update_service.h"
#include "roamex/browser/updates/roamex_update_service_factory.h"
#endif

namespace roamex {

RoamexAboutUI::RoamexAboutUI(content::WebUI *web_ui)
    : ui::MojoWebUIController(web_ui) {
  content::WebUIDataSource *source = content::WebUIDataSource::CreateAndAdd(
      Profile::FromWebUI(web_ui), kChromeUIRoamexAboutHost);

  source->AddString("productName", "Roamex");
  source->AddString("version", std::string(version_info::GetVersionNumber()));
  // The live update card is available only when the Sparkle-backed service is
  // compiled in; flag-off the TS renders the "unavailable" state.
  source->AddBoolean("updatesAvailable", BUILDFLAG(ROAMEX_ENABLE_SPARKLE) != 0);

  webui::SetupWebUIDataSource(source, base::span(kRoamexAboutResources),
                              IDR_ROAMEX_ABOUT_ABOUT_HTML);
}

RoamexAboutUI::~RoamexAboutUI() = default;

#if BUILDFLAG(ROAMEX_ENABLE_SPARKLE)
void RoamexAboutUI::BindInterface(
    mojo::PendingReceiver<mojom::UpdatePageHandlerFactory> receiver) {
  updates::RoamexUpdateService *service =
      updates::RoamexUpdateServiceFactory::GetForProfile(
          Profile::FromWebUI(web_ui()));
  if (service) {
    service->BindFactory(std::move(receiver));
  }
}
#endif

WEB_UI_CONTROLLER_TYPE_IMPL(RoamexAboutUI)

} // namespace roamex

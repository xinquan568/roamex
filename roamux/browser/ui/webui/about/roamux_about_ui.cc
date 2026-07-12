// SPDX-License-Identifier: Apache-2.0
#include "roamux/browser/ui/webui/about/roamux_about_ui.h"

#include "base/containers/span.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/grit/roamux_about_resources.h"
#include "chrome/grit/roamux_about_resources_map.h"
#include "components/version_info/version_info.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "ui/webui/webui_util.h"

#if BUILDFLAG(ROAMUX_ENABLE_SPARKLE)
#include "roamux/browser/updates/roamux_update_service.h"
#include "roamux/browser/updates/roamux_update_service_factory.h"
#endif

namespace roamux {

RoamuxAboutUI::RoamuxAboutUI(content::WebUI *web_ui)
    : ui::MojoWebUIController(web_ui) {
  content::WebUIDataSource *source = content::WebUIDataSource::CreateAndAdd(
      Profile::FromWebUI(web_ui), kChromeUIRoamuxAboutHost);

  source->AddString("productName", "Roamux");
  source->AddString("version", std::string(version_info::GetVersionNumber()));

  // The live update card is available only when the Sparkle-backed service is
  // both compiled in AND present for this profile (regular profiles only —
  // OTR/system get no service). Otherwise the TS renders the static
  // "updates unavailable" state and never issues Mojo calls.
  bool updates_available = false;
#if BUILDFLAG(ROAMUX_ENABLE_SPARKLE)
  updates_available = updates::RoamuxUpdateServiceFactory::GetForProfile(
                          Profile::FromWebUI(web_ui)) != nullptr;
#endif
  source->AddBoolean("updatesAvailable", updates_available);

  webui::SetupWebUIDataSource(source, base::span(kRoamuxAboutResources),
                              IDR_ROAMUX_ABOUT_ABOUT_HTML);
}

RoamuxAboutUI::~RoamuxAboutUI() = default;

#if BUILDFLAG(ROAMUX_ENABLE_SPARKLE)
void RoamuxAboutUI::BindInterface(
    mojo::PendingReceiver<mojom::UpdatePageHandlerFactory> receiver) {
  updates::RoamuxUpdateService *service =
      updates::RoamuxUpdateServiceFactory::GetForProfile(
          Profile::FromWebUI(web_ui()));
  if (service) {
    service->BindFactory(std::move(receiver));
  }
}
#endif

WEB_UI_CONTROLLER_TYPE_IMPL(RoamuxAboutUI)

} // namespace roamux

// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_BROWSER_UI_WEBUI_ABOUT_ROAMUX_ABOUT_UI_H_
#define ROAMUX_BROWSER_UI_WEBUI_ABOUT_ROAMUX_ABOUT_UI_H_

#include "content/public/browser/internal_webui_config.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "roamux/app/app_buildflags.h"
#include "roamux/common/roamux_url_constants.h"
#include "roamux/mojom/update_page.mojom.h"
#include "ui/webui/mojo_web_ui_controller.h"

namespace roamux {

// kChromeUIRoamuxAboutHost moved to roamux/common/roamux_url_constants.h
// (roam-91) so the scheme-alias map can reference it without depping this
// WebUI target.

class RoamuxAboutUI;

// chrome://roamux-about (roam-37): the About page + custom update UI, bound to
// roam-85's RoamuxUpdateService via the UpdatePageHandlerFactory. The service
// only compiles under roamux_enable_sparkle; flag-off the page still serves
// (identity + links) with the update card degraded.
class RoamuxAboutUIConfig
    : public content::DefaultInternalWebUIConfig<RoamuxAboutUI> {
public:
  RoamuxAboutUIConfig()
      : DefaultInternalWebUIConfig(kChromeUIRoamuxAboutHost) {}
};

class RoamuxAboutUI : public ui::MojoWebUIController {
public:
  explicit RoamuxAboutUI(content::WebUI *web_ui);
  ~RoamuxAboutUI() override;

  RoamuxAboutUI(const RoamuxAboutUI &) = delete;
  RoamuxAboutUI &operator=(const RoamuxAboutUI &) = delete;

#if BUILDFLAG(ROAMUX_ENABLE_SPARKLE)
  // Delegates to RoamuxUpdateService (which implements the factory).
  void BindInterface(
      mojo::PendingReceiver<mojom::UpdatePageHandlerFactory> receiver);
#endif

private:
  WEB_UI_CONTROLLER_TYPE_DECL();
};

} // namespace roamux

#endif // ROAMUX_BROWSER_UI_WEBUI_ABOUT_ROAMUX_ABOUT_UI_H_

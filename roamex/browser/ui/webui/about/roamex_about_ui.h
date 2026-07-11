// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMEX_BROWSER_UI_WEBUI_ABOUT_ROAMEX_ABOUT_UI_H_
#define ROAMEX_BROWSER_UI_WEBUI_ABOUT_ROAMEX_ABOUT_UI_H_

#include "content/public/browser/internal_webui_config.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "roamex/app/app_buildflags.h"
#include "roamex/common/roamex_url_constants.h"
#include "roamex/mojom/update_page.mojom.h"
#include "ui/webui/mojo_web_ui_controller.h"

namespace roamex {

// kChromeUIRoamexAboutHost moved to roamex/common/roamex_url_constants.h
// (roam-91) so the scheme-alias map can reference it without depping this
// WebUI target.

class RoamexAboutUI;

// chrome://roamux-about (roam-37): the About page + custom update UI, bound to
// roam-85's RoamexUpdateService via the UpdatePageHandlerFactory. The service
// only compiles under roamex_enable_sparkle; flag-off the page still serves
// (identity + links) with the update card degraded.
class RoamexAboutUIConfig
    : public content::DefaultInternalWebUIConfig<RoamexAboutUI> {
public:
  RoamexAboutUIConfig()
      : DefaultInternalWebUIConfig(kChromeUIRoamexAboutHost) {}
};

class RoamexAboutUI : public ui::MojoWebUIController {
public:
  explicit RoamexAboutUI(content::WebUI *web_ui);
  ~RoamexAboutUI() override;

  RoamexAboutUI(const RoamexAboutUI &) = delete;
  RoamexAboutUI &operator=(const RoamexAboutUI &) = delete;

#if BUILDFLAG(ROAMEX_ENABLE_SPARKLE)
  // Delegates to RoamexUpdateService (which implements the factory).
  void BindInterface(
      mojo::PendingReceiver<mojom::UpdatePageHandlerFactory> receiver);
#endif

private:
  WEB_UI_CONTROLLER_TYPE_DECL();
};

} // namespace roamex

#endif // ROAMEX_BROWSER_UI_WEBUI_ABOUT_ROAMEX_ABOUT_UI_H_

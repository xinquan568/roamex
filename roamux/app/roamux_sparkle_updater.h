// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_APP_ROAMUX_SPARKLE_UPDATER_H_
#define ROAMUX_APP_ROAMUX_SPARKLE_UPDATER_H_

// Sparkle updater surface (roam-32, plan §13.6/K4), compiled only when
// roamux_enable_sparkle=true; the patch-0025 hunks in app_controller_mac.mm /
// main_menu_builder.mm are guarded by BUILDFLAG(ROAMUX_ENABLE_SPARKLE).
namespace roamux::app {

// Creates the process-wide SPUStandardUpdaterController (scheduled checks per
// the Info.plist keys). Idempotent; call from applicationDidFinishLaunching.
void InitSparkleUpdater();

// The "Check for Updates…" menu action.
void CheckForUpdates();

// roam-85: the update service installs a handler here so the menu action
// routes to the single process-wide Sparkle owner (one updater, not two).
// When unset (e.g. the service isn't built), CheckForUpdates() falls back to
// the standard controller.
void SetCheckForUpdatesHandler(void (*handler)());

}  // namespace roamux::app

#endif  // ROAMUX_APP_ROAMUX_SPARKLE_UPDATER_H_

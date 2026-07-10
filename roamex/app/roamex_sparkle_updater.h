// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMEX_APP_ROAMEX_SPARKLE_UPDATER_H_
#define ROAMEX_APP_ROAMEX_SPARKLE_UPDATER_H_

// Sparkle updater surface (roam-32, plan §13.6/K4), compiled only when
// roamex_enable_sparkle=true; the patch-0025 hunks in app_controller_mac.mm /
// main_menu_builder.mm are guarded by BUILDFLAG(ROAMEX_ENABLE_SPARKLE).
namespace roamex::app {

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

}  // namespace roamex::app

#endif  // ROAMEX_APP_ROAMEX_SPARKLE_UPDATER_H_

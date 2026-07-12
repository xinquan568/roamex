// SPDX-License-Identifier: Apache-2.0
#include "roamux/app/roamux_sparkle_updater.h"

#import <AppKit/AppKit.h>
#import <Sparkle/Sparkle.h>

namespace {
// Process-wide standard controller (owns SPUUpdater + the standard UI
// driver). Scheduled checks follow the Info.plist keys merged by roam-32.
SPUStandardUpdaterController* g_updater_controller = nil;
// roam-85: when the update service is built, it installs its check handler
// here so exactly one Sparkle owner services the menu action.
void (*g_check_handler)() = nullptr;
}  // namespace

namespace roamux::app {

void InitSparkleUpdater() {
  if (g_updater_controller) {
    return;
  }
  g_updater_controller =
      [[SPUStandardUpdaterController alloc] initWithStartingUpdater:YES
                                                    updaterDelegate:nil
                                                 userDriverDelegate:nil];
}

void CheckForUpdates() {
  // roam-85: route to the service's process-wide owner when present.
  if (g_check_handler) {
    g_check_handler();
    return;
  }
  InitSparkleUpdater();
  [g_updater_controller checkForUpdates:nil];
}

void SetCheckForUpdatesHandler(void (*handler)()) {
  g_check_handler = handler;
}

}  // namespace roamux::app

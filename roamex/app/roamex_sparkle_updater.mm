// SPDX-License-Identifier: Apache-2.0
#include "roamex/app/roamex_sparkle_updater.h"

#import <AppKit/AppKit.h>
#import <Sparkle/Sparkle.h>

namespace {
// Process-wide standard controller (owns SPUUpdater + the standard UI
// driver). Scheduled checks follow the Info.plist keys merged by roam-32.
SPUStandardUpdaterController* g_updater_controller = nil;
}  // namespace

namespace roamex::app {

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
  InitSparkleUpdater();
  [g_updater_controller checkForUpdates:nil];
}

}  // namespace roamex::app

// SPDX-License-Identifier: Apache-2.0
#include "roamux/browser/ui/views/tabs/tab_strip_toggle_command.h"

#include <map>

#include "base/feature_list.h"
#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/features.h"
#include "components/prefs/pref_service.h"
#include "roamux/common/roamux_features.h"
#include "roamux/common/tab_strip_placement.h"

namespace roamux::tabs_toggle {

namespace {

std::map<BrowserWindowInterface*, base::RepeatingClosure>& Handlers() {
  static base::NoDestructor<
      std::map<BrowserWindowInterface*, base::RepeatingClosure>>
      handlers;
  return *handlers;
}

std::map<Browser*, RoamuxTabStripSignalHooks>& Hooks() {
  static base::NoDestructor<std::map<Browser*, RoamuxTabStripSignalHooks>>
      hooks;
  return *hooks;
}

}  // namespace

bool CanToggleTabStrip(BrowserWindowInterface* browser) {
  if (!browser ||
      !base::FeatureList::IsEnabled(features::kTabStripToggleShortcut)) {
    return false;
  }
  if (!tabs::IsVerticalTabsFeatureEnabled()) {
    return false;
  }
  const TabStripPlacement placement =
      GetTabStripPlacement(browser->GetProfile()->GetPrefs());
  return placement == TabStripPlacement::kLeft ||
         placement == TabStripPlacement::kRight;
}

void ToggleTabStrip(BrowserWindowInterface* browser) {
  auto it = Handlers().find(browser);
  if (it != Handlers().end()) {
    it->second.Run();
  }
  // No handler (0053-only build, or flag-off window): deliberate no-op.
}

void SetToggleHandler(BrowserWindowInterface* browser,
                      base::RepeatingClosure handler) {
  Handlers()[browser] = std::move(handler);
}

void ClearToggleHandler(BrowserWindowInterface* browser) {
  Handlers().erase(browser);
}

RoamuxTabStripSignalHooks::RoamuxTabStripSignalHooks() = default;
RoamuxTabStripSignalHooks::RoamuxTabStripSignalHooks(
    RoamuxTabStripSignalHooks&&) = default;
RoamuxTabStripSignalHooks& RoamuxTabStripSignalHooks::operator=(
    RoamuxTabStripSignalHooks&&) = default;
RoamuxTabStripSignalHooks::~RoamuxTabStripSignalHooks() = default;

void SetSignalHooks(Browser* browser, RoamuxTabStripSignalHooks hooks) {
  Hooks()[browser] = std::move(hooks);
}

void ClearSignalHooks(Browser* browser) {
  Hooks().erase(browser);
}

}  // namespace roamux::tabs_toggle

namespace roamux {

void OnVerticalTabStripRegionViewCreated(
    Browser* browser,
    VerticalTabStripRegionView* region_view) {
  auto it = tabs_toggle::Hooks().find(browser);
  if (it != tabs_toggle::Hooks().end() && it->second.region_created) {
    it->second.region_created.Run(region_view);
  }
}

void OnVerticalTabStripRegionViewDestroyed(
    Browser* browser,
    VerticalTabStripRegionView* region_view) {
  auto it = tabs_toggle::Hooks().find(browser);
  if (it != tabs_toggle::Hooks().end() && it->second.region_destroyed) {
    it->second.region_destroyed.Run(region_view);
  }
}

void OnVerticalTabStripMenuCommandExecuted(Browser* browser) {
  auto it = tabs_toggle::Hooks().find(browser);
  if (it != tabs_toggle::Hooks().end() && it->second.menu_executed) {
    it->second.menu_executed.Run();
  }
}

void OnVerticalTabStripUserActivation(const BrowserWindowInterface* browser) {
  if (!browser) {
    return;
  }
  Browser* key = const_cast<BrowserWindowInterface*>(browser)
                     ->GetBrowserForMigrationOnly();
  auto it = tabs_toggle::Hooks().find(key);
  if (it != tabs_toggle::Hooks().end() && it->second.user_activation) {
    it->second.user_activation.Run();
  }
}

}  // namespace roamux

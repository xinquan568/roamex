// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_BROWSER_UI_WEBUI_ROAMUX_SHORTCUTS_HANDLER_H_
#define ROAMUX_BROWSER_UI_WEBUI_ROAMUX_SHORTCUTS_HANDLER_H_

#include <string>

#include "base/values.h"
#include "chrome/browser/ui/webui/settings/settings_page_ui_handler.h"

namespace roamux {

// The §4.3 shared shortcut surface's message handler (roam-13): lists the
// ENABLED registry entries with their current chords; validates + persists
// rebinds. All validation is C++-side; the page only records chords.
// (Registered at the settings handler registration point via patch 0011 —
// the §12.2-declared additive roamux WebUI handler.)
class RoamuxShortcutsHandler : public settings::SettingsPageUIHandler {
 public:
  RoamuxShortcutsHandler();
  RoamuxShortcutsHandler(const RoamuxShortcutsHandler&) = delete;
  RoamuxShortcutsHandler& operator=(const RoamuxShortcutsHandler&) = delete;
  ~RoamuxShortcutsHandler() override;

  // SettingsPageUIHandler:
  void RegisterMessages() override;
  void OnJavascriptAllowed() override {}
  void OnJavascriptDisallowed() override {}

  // Testing seams (drive the message paths directly).
  base::ListValue GetShortcutListForTesting();
  std::string RebindForTesting(const std::string& pref_key,
                               const base::DictValue& chord_dict);

 private:
  void HandleGetShortcuts(const base::ListValue& args);
  void HandleRebindShortcut(const base::ListValue& args);

  base::ListValue BuildShortcutList();
  // Returns "" on success, else the rejection reason
  // ("invalid" | "reserved" | "roamux").
  std::string Rebind(const std::string& pref_key,
                     const base::DictValue& chord_dict);
};

}  // namespace roamux

#endif  // ROAMUX_BROWSER_UI_WEBUI_ROAMUX_SHORTCUTS_HANDLER_H_

// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_BROWSER_TABS_SHORTCUT_REGISTRY_MAC_H_
#define ROAMUX_BROWSER_TABS_SHORTCUT_REGISTRY_MAC_H_

#include <string>
#include <vector>

#include "roamux/browser/tabs/shortcut_registry.h"

#ifdef __OBJC__
@class NSEvent;
#else
class NSEvent;
#endif

namespace roamux::tabs {

// Resolves a key event against the ENABLED roamux bindings (pref override or
// default) of the last-active browser's profile. Returns the command id or
// -1. The dispatch hook (patch 0011) calls this from CommandForKeyEvent AFTER
// the main-menu check.
int CommandForKeyEventOverride(NSEvent* event);

// The §4.3 runtime-enumerable reserved set: the AcceleratorsCocoa table, the
// non-menu shortcut table, and the main menu's key equivalents.
// `exclude_command_id` drops that command's OWN accelerator-table row so a
// user can rebind back to a Roamux default (other rows still conflict).
std::vector<Chord> EnumerateReservedChords(int exclude_command_id);

// Maps a DOM `KeyboardEvent.code` string (from the WebUI recorder, e.g.
// "KeyY") into the Carbon keycode domain the registry uses. Returns -1 when
// unmappable. Layout-independent (positional), matching NSEvent.keyCode.
int CarbonKeycodeFromDomCodeString(const std::string& code);

}  // namespace roamux::tabs

#endif  // ROAMUX_BROWSER_TABS_SHORTCUT_REGISTRY_MAC_H_

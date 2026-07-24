// SPDX-License-Identifier: Apache-2.0
#include "roamux/browser/tabs/shortcut_registry_mac.h"

#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>

#include "chrome/browser/browser_process.h"
#include "chrome/browser/global_keyboard_shortcuts_mac.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/cocoa/accelerators_cocoa.h"
#include "roamux/browser/ui/views/tabs/tab_strip_toggle_command.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/base_window.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/dom/keycode_converter.h"
#include "ui/events/keycodes/keyboard_code_conversion_mac.h"

namespace roamux::tabs {

namespace {

// Keep in sync with chrome/app/chrome_command_ids.h (roam-214 patch 0053)
// and shortcut_registry.cc.
constexpr int kIdcToggleTabStrip = 33012;

Chord ChordFromEvent(NSEvent* event) {
  const NSUInteger modifiers = [event modifierFlags];
  Chord chord;
  chord.cmd = (modifiers & NSEventModifierFlagCommand) != 0;
  chord.shift = (modifiers & NSEventModifierFlagShift) != 0;
  chord.ctrl = (modifiers & NSEventModifierFlagControl) != 0;
  chord.opt = (modifiers & NSEventModifierFlagOption) != 0;
  chord.keycode = [event keyCode];
  return chord;
}

void CollectMenuKeyEquivalents(NSMenu* menu, std::vector<Chord>* out) {
  for (NSMenuItem* item in [menu itemArray]) {
    NSString* key_equivalent = [item keyEquivalent];
    if ([key_equivalent length] > 0) {
      const NSUInteger mask = [item keyEquivalentModifierMask];
      Chord chord;
      chord.cmd = (mask & NSEventModifierFlagCommand) != 0;
      chord.shift = (mask & NSEventModifierFlagShift) != 0;
      chord.ctrl = (mask & NSEventModifierFlagControl) != 0;
      chord.opt = (mask & NSEventModifierFlagOption) != 0;
      const unichar ch = [key_equivalent characterAtIndex:0];
      ui::KeyboardCode key_code = ui::KeyboardCodeFromCharCode(ch);
      chord.keycode =
          ui::MacKeyCodeForWindowsKeyCode(key_code, 0, nullptr, nullptr);
      if (chord.keycode >= 0) {
        out->push_back(chord);
      }
    }
    if ([item hasSubmenu]) {
      CollectMenuKeyEquivalents([item submenu], out);
    }
  }
}

}  // namespace

namespace {

// roam-214 (plan R1): the browser owning the EVENT's window — it can differ
// from the last-active browser across profiles or redispatch.
BrowserWindowInterface* BrowserForEventWindow(NSEvent* event) {
  NSWindow* event_window = [event window];
  if (!event_window) {
    return nullptr;
  }
  for (BrowserWindowInterface* candidate : GetAllBrowserWindowInterfaces()) {
    if (candidate->GetWindow() &&
        candidate->GetWindow()->GetNativeWindow().GetNativeNSWindow() ==
            event_window) {
      return candidate;
    }
  }
  return nullptr;
}

}  // namespace

int CommandForKeyEventOverride(NSEvent* event) {
  const Chord chord = ChordFromEvent(event);
  BrowserWindowInterface* last_active = chrome::FindLastActive();

  // roam-214 (plan R1), scoped to the toggle command ONLY — existing
  // shortcuts keep their historical last-active resolution below. BOTH the
  // chord binding (the rebind dict is a profile pref) AND availability are
  // resolved against the browser owning the event's window, so a window can
  // neither miss its own profile's binding nor accept another profile's.
  // Unmappable window -> the toggle never claims the event.
  if (BrowserWindowInterface* target = BrowserForEventWindow(event)) {
    const int target_id = CommandForChord(target->GetProfile()->GetPrefs(),
                                          AllShortcuts(), chord);
    if (target_id == kIdcToggleTabStrip) {
      return tabs_toggle::CanToggleTabStrip(target) ? target_id : -1;
    }
  }

  if (!last_active) {
    return -1;
  }
  const int command_id = CommandForChord(last_active->GetProfile()->GetPrefs(),
                                         AllShortcuts(), chord);
  if (command_id == kIdcToggleTabStrip) {
    // The last-active profile binds this chord to the toggle, but the
    // event's window did not resolve it (different profile/binding or
    // unmappable): never execute the toggle against a window that did not
    // bind it.
    return -1;
  }
  return command_id;
}

std::vector<Chord> EnumerateReservedChords(int exclude_command_id) {
  std::vector<Chord> reserved;
  // 1) The AcceleratorsCocoa table (ui::Accelerator: EF_* + VKEY_*).
  AcceleratorsCocoa* accelerators = AcceleratorsCocoa::GetInstance();
  for (const auto& [command, accelerator] : *accelerators) {
    if (command == exclude_command_id) {
      continue;  // The command's own default row is not a conflict.
    }
    Chord chord;
    chord.cmd = (accelerator.modifiers() & ui::EF_COMMAND_DOWN) != 0;
    chord.shift = (accelerator.modifiers() & ui::EF_SHIFT_DOWN) != 0;
    chord.ctrl = (accelerator.modifiers() & ui::EF_CONTROL_DOWN) != 0;
    chord.opt = (accelerator.modifiers() & ui::EF_ALT_DOWN) != 0;
    chord.keycode = ui::MacKeyCodeForWindowsKeyCode(accelerator.key_code(), 0,
                                                    nullptr, nullptr);
    if (chord.keycode >= 0) {
      reserved.push_back(chord);
    }
  }
  // 2) The non-menu shortcut table (already in Carbon keycodes).
  for (const auto& data : GetShortcutsNotPresentInMainMenu()) {
    reserved.push_back(Chord{.cmd = data.command_key,
                             .shift = data.shift_key,
                             .ctrl = data.cntrl_key,
                             .opt = data.opt_key,
                             .keycode = data.vkey_code});
  }
  // 3) The main menu's key equivalents.
  if (NSMenu* main_menu = [NSApp mainMenu]) {
    CollectMenuKeyEquivalents(main_menu, &reserved);
  }
  return reserved;
}

int CarbonKeycodeFromDomCodeString(const std::string& code) {
  const ui::DomCode dom_code = ui::KeycodeConverter::CodeStringToDomCode(code);
  if (dom_code == ui::DomCode::NONE) {
    return -1;
  }
  return ui::KeycodeConverter::DomCodeToNativeKeycode(dom_code);
}

}  // namespace roamux::tabs

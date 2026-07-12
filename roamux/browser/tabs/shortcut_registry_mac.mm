// SPDX-License-Identifier: Apache-2.0
#include "roamux/browser/tabs/shortcut_registry_mac.h"

#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>

#include "chrome/browser/browser_process.h"
#include "chrome/browser/global_keyboard_shortcuts_mac.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/cocoa/accelerators_cocoa.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/dom/keycode_converter.h"
#include "ui/events/keycodes/keyboard_code_conversion_mac.h"

namespace roamux::tabs {

namespace {

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

int CommandForKeyEventOverride(NSEvent* event) {
  BrowserWindowInterface* browser = chrome::FindLastActive();
  if (!browser) {
    return -1;
  }
  return CommandForChord(browser->GetProfile()->GetPrefs(), AllShortcuts(),
                         ChordFromEvent(event));
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

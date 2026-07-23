// SPDX-License-Identifier: Apache-2.0
#include "roamux/browser/ui/webui/chord_display.h"

#import <Carbon/Carbon.h>

#include "base/apple/scoped_cftyperef.h"
#include "base/i18n/case_conversion.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/events/keycodes/dom/keycode_converter.h"
#include "ui/events/keycodes/keyboard_code_conversion_mac.h"

namespace roamux {

namespace {

// Displayable = a real, single, non-control code unit. U+FFFD is the
// conversion API's no-layout-data sentinel; surrogates cannot stand alone;
// IsUnicodeControl covers C0, DEL, and C1.
bool IsDisplayableKeyChar(UniChar ch) {
  if (ch == 0 || ch == 0xFFFD) {
    return false;
  }
  if (base::IsUnicodeControl(ch)) {
    return false;
  }
  if (ch >= 0xD800 && ch <= 0xDFFF) {
    return false;
  }
  return true;
}

}  // namespace

std::string ChordKeyDisplayString(uint16_t keycode) {
  // (1) Layout-aware: the character the current keyboard layout produces.
  base::apple::ScopedCFTypeRef<TISInputSourceRef> source(
      TISCopyCurrentKeyboardLayoutInputSource());
  UInt32 dead_key_state = 0;
  const UniChar ch = ui::TranslatedUnicodeCharFromKeyCode(
      source.get(), keycode, kUCKeyActionDisplay, /*modifier_key_state=*/0,
      LMGetKbdType(), &dead_key_state);
  if (IsDisplayableKeyChar(ch)) {
    // Current-locale ICU uppercasing; the result may expand — accepted as-is.
    return base::UTF16ToUTF8(base::i18n::ToUpper(std::u16string(1, ch)));
  }

  // (2) Canonical accelerator name (the F1/F6/F11-class named keys).
  const ui::KeyboardCode key = ui::KeyboardCodeFromKeyCode(keycode);
  const std::u16string accel_text =
      ui::Accelerator(key, ui::EF_NONE).GetShortcutText();
  if (!accel_text.empty()) {
    return base::UTF16ToUTF8(accel_text);
  }

  // (3) Named DOM code (renders e.g. "F5").
  const ui::DomCode dom_code =
      ui::KeycodeConverter::NativeKeycodeToDomCode(keycode);
  const std::string code_string =
      ui::KeycodeConverter::DomCodeToCodeString(dom_code);
  if (!code_string.empty()) {
    return code_string;
  }

  // (4) Floor — a glyph, never a number.
  return "?";
}

}  // namespace roamux

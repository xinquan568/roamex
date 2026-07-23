// SPDX-License-Identifier: Apache-2.0
// roam-13 (I-2.4): rebind persists; conflicting chords are rejected (both
// classes); dispatch honors the default AND the override through the real
// CommandForKeyEvent path (closing the roam-12 gap); flag-off is inert.

#import <Carbon/Carbon.h>
#import <Cocoa/Cocoa.h>

#include "base/apple/scoped_cftyperef.h"
#include "base/strings/string_util.h"
#include "base/test/scoped_feature_list.h"
#include "base/values.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/global_keyboard_shortcuts_mac.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/test_web_ui.h"
#include "roamux/browser/tabs/shortcut_registry.h"
#include "roamux/browser/ui/webui/roamux_shortcuts_handler.h"
#include "roamux/common/roamux_features.h"
#include "roamux/common/roamux_prefs.h"
#include "roamux/test/support/roamux_browser_test.h"
#include "ui/events/keycodes/keyboard_code_conversion_mac.h"
#include "ui/events/test/cocoa_test_event_utils.h"

namespace roamux {
namespace {

NSEvent* KeyEventForChord(const tabs::Chord& chord) {
  NSUInteger modifiers = 0;
  if (chord.cmd) {
    modifiers |= NSEventModifierFlagCommand;
  }
  if (chord.shift) {
    modifiers |= NSEventModifierFlagShift;
  }
  if (chord.ctrl) {
    modifiers |= NSEventModifierFlagControl;
  }
  if (chord.opt) {
    modifiers |= NSEventModifierFlagOption;
  }
  return cocoa_test_event_utils::KeyEventWithKeyCode(
      chord.keycode, 'r', NSEventTypeKeyDown, modifiers);
}

class ExposedHandler : public RoamuxShortcutsHandler {
 public:
  using RoamuxShortcutsHandler::set_web_ui;
};

// roam-207: the chordText the handler renders for `pref_key`, or "" if absent.
std::string ChordTextFor(RoamuxShortcutsHandler& handler,
                         const std::string& pref_key) {
  base::ListValue list = handler.GetShortcutListForTesting();
  for (const base::Value& item : list) {
    const std::string* key = item.GetDict().FindString("key");
    if (key && *key == pref_key) {
      const std::string* text = item.GetDict().FindString("chordText");
      return text ? *text : "";
    }
  }
  return "";
}

// roam-207 T1 preflight: the exact production translation of `keycode` under
// the current input source (mirrors ChordKeyDisplayString step 1).
UniChar TranslateForDisplay(uint16_t keycode, std::string* source_id) {
  base::apple::ScopedCFTypeRef<TISInputSourceRef> source(
      TISCopyCurrentKeyboardLayoutInputSource());
  if (source_id) {
    NSString* ns_id = (__bridge NSString*)TISGetInputSourceProperty(
        source.get(), kTISPropertyInputSourceID);
    *source_id = ns_id ? [ns_id UTF8String] : "<unknown>";
  }
  UInt32 dead_key_state = 0;
  return ui::TranslatedUnicodeCharFromKeyCode(source.get(), keycode,
                                              kUCKeyActionDisplay, 0,
                                              LMGetKbdType(), &dead_key_state);
}

class RoamuxShortcutsTest : public roamux::test::RoamuxBrowserTest {
 public:
  RoamuxShortcutsTest() {
    features_.InitAndEnableFeature(features::kInitialUrl);
  }

 protected:
  PrefService* prefs() { return browser()->profile()->GetPrefs(); }

  std::string Rebind(bool cmd,
                     bool shift,
                     bool ctrl,
                     bool opt,
                     const std::string& dom_code) {
    content::TestWebUI test_web_ui;
    test_web_ui.set_web_contents(
        browser()->tab_strip_model()->GetActiveWebContents());
    ExposedHandler handler;
    handler.set_web_ui(&test_web_ui);
    base::DictValue chord;
    chord.Set("cmd", cmd);
    chord.Set("shift", shift);
    chord.Set("ctrl", ctrl);
    chord.Set("opt", opt);
    chord.Set("code", dom_code);
    return handler.RebindForTesting("reload_initial_url", chord);
  }

  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(RoamuxShortcutsTest, DefaultChordDispatches) {
  // The roam-12 gap closure: the DEFAULT Ctrl+Cmd+R resolves through the real
  // key-event path.
  const tabs::Chord default_chord =
      tabs::GetCurrentChord(prefs(), tabs::AllShortcuts()[0]);
  CommandForKeyEventResult result =
      CommandForKeyEvent(KeyEventForChord(default_chord));
  EXPECT_TRUE(result.found());
  EXPECT_EQ(IDC_RELOAD_INITIAL_URL, result.chrome_command);
}

IN_PROC_BROWSER_TEST_F(RoamuxShortcutsTest, RebindPersistsAndRedispatches) {
  const tabs::Chord default_chord =
      tabs::GetCurrentChord(prefs(), tabs::AllShortcuts()[0]);
  // The recorder sends the DOM code string; persisted in the CARBON domain.
  EXPECT_EQ("", Rebind(true, false, true, false, "KeyY"));
  const tabs::Chord new_chord{.cmd = true, .ctrl = true, .keycode = kVK_ANSI_Y};

  // Persisted in the pref…
  const base::DictValue& bindings = prefs()->GetDict(prefs::kShortcutBindings);
  ASSERT_NE(nullptr, bindings.FindDict("reload_initial_url"));
  // …the registry answers the new chord…
  EXPECT_EQ(new_chord, tabs::GetCurrentChord(prefs(), tabs::AllShortcuts()[0]));
  // …and dispatch follows: old chord dead, new chord live.
  EXPECT_FALSE(CommandForKeyEvent(KeyEventForChord(default_chord)).found());
  CommandForKeyEventResult result =
      CommandForKeyEvent(KeyEventForChord(new_chord));
  EXPECT_TRUE(result.found());
  EXPECT_EQ(IDC_RELOAD_INITIAL_URL, result.chrome_command);
}

IN_PROC_BROWSER_TEST_F(RoamuxShortcutsTest, ConflictingChordsRejected) {
  // UI-entered Cmd+R ("KeyR") maps to Carbon kVK_ANSI_R and must be rejected
  // as the browser reload chord.
  EXPECT_EQ("reserved", Rebind(true, false, false, false, "KeyR"));
  // Invalid: shift-only modifier; unmappable code string.
  EXPECT_EQ("invalid", Rebind(false, true, false, false, "KeyR"));
  EXPECT_EQ("invalid", Rebind(true, false, true, false, "NotACode"));
  // No pref writes happened.
  EXPECT_EQ(nullptr, prefs()
                         ->GetDict(prefs::kShortcutBindings)
                         .FindDict("reload_initial_url"));
}

class RoamuxShortcutsFlagOffTest : public roamux::test::RoamuxBrowserTest {
 public:
  RoamuxShortcutsFlagOffTest() {
    features_.InitAndDisableFeature(features::kInitialUrl);
  }

 protected:
  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(RoamuxShortcutsFlagOffTest, DispatchInertWhenFlagOff) {
  const tabs::Chord default_chord = tabs::AllShortcuts()[0].default_chord;
  NSUInteger modifiers =
      NSEventModifierFlagCommand | NSEventModifierFlagControl;
  NSEvent* event = cocoa_test_event_utils::KeyEventWithKeyCode(
      default_chord.keycode, 'r', NSEventTypeKeyDown, modifiers);
  EXPECT_FALSE(CommandForKeyEvent(event).found());
}

// roam-207: shortcut rows render the KEY character, not the raw keycode.
IN_PROC_BROWSER_TEST_F(RoamuxShortcutsTest,
                       DefaultChordTextRendersKeyCharacters) {
  // Mechanical layout preflight: the literals below assume the builder's
  // US-ANSI layout; skip loudly (with the observed translations) otherwise.
  std::string source_id;
  const UniChar r = TranslateForDisplay(kVK_ANSI_R, &source_id);
  const UniChar lb = TranslateForDisplay(kVK_ANSI_LeftBracket, nullptr);
  const UniChar rb = TranslateForDisplay(kVK_ANSI_RightBracket, nullptr);
  if (r != 'r' || lb != '[' || rb != ']') {
    GTEST_SKIP() << "non-US-ANSI layout (" << source_id << "): observed U+"
                 << std::hex << r << "/U+" << lb << "/U+" << rb;
  }

  content::TestWebUI test_web_ui;
  test_web_ui.set_web_contents(
      browser()->tab_strip_model()->GetActiveWebContents());
  ExposedHandler handler;
  handler.set_web_ui(&test_web_ui);

  EXPECT_EQ("\xE2\x8C\x83\xE2\x8C\x98R",
            ChordTextFor(handler, "reload_initial_url"));
  EXPECT_EQ("\xE2\x8C\x83\xE2\x8C\x98[",
            ChordTextFor(handler, "tab_visit_back"));
  EXPECT_EQ("\xE2\x8C\x83\xE2\x8C\x98]",
            ChordTextFor(handler, "tab_visit_forward"));
}

// roam-207: an unmappable (function) key renders a NAMED key — never a
// numeric placeholder.
IN_PROC_BROWSER_TEST_F(RoamuxShortcutsTest, UnmappableKeycodeRendersNamedKey) {
  EXPECT_EQ("", Rebind(true, false, true, false, "F5"));

  content::TestWebUI test_web_ui;
  test_web_ui.set_web_contents(
      browser()->tab_strip_model()->GetActiveWebContents());
  ExposedHandler handler;
  handler.set_web_ui(&test_web_ui);

  const std::string text = ChordTextFor(handler, "reload_initial_url");
  EXPECT_TRUE(base::EndsWith(text, "F5")) << text;
  EXPECT_EQ(std::string::npos, text.find('[')) << text;
}

}  // namespace
}  // namespace roamux

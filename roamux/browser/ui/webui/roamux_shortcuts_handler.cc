// SPDX-License-Identifier: Apache-2.0
#include "roamux/browser/ui/webui/roamux_shortcuts_handler.h"

#include <string>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/web_ui.h"
#include "roamux/browser/tabs/shortcut_registry.h"
#include "roamux/browser/tabs/shortcut_registry_mac.h"

namespace roamux {

namespace {

std::string ChordDisplayString(const tabs::Chord& chord) {
  std::string text;
  if (chord.ctrl) {
    text += "\xE2\x8C\x83";  // ⌃
  }
  if (chord.opt) {
    text += "\xE2\x8C\xA5";  // ⌥
  }
  if (chord.shift) {
    text += "\xE2\x87\xA7";  // ⇧
  }
  if (chord.cmd) {
    text += "\xE2\x8C\x98";  // ⌘
  }
  text += "[" + base::NumberToString(chord.keycode) + "]";
  return text;
}

}  // namespace

RoamuxShortcutsHandler::RoamuxShortcutsHandler() = default;
RoamuxShortcutsHandler::~RoamuxShortcutsHandler() = default;

void RoamuxShortcutsHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "roamuxGetShortcuts",
      base::BindRepeating(&RoamuxShortcutsHandler::HandleGetShortcuts,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "roamuxRebindShortcut",
      base::BindRepeating(&RoamuxShortcutsHandler::HandleRebindShortcut,
                          base::Unretained(this)));
}

base::ListValue RoamuxShortcutsHandler::GetShortcutListForTesting() {
  return BuildShortcutList();
}

std::string RoamuxShortcutsHandler::RebindForTesting(
    const std::string& pref_key,
    const base::DictValue& chord_dict) {
  return Rebind(pref_key, chord_dict);
}

void RoamuxShortcutsHandler::HandleGetShortcuts(const base::ListValue& args) {
  AllowJavascript();
  const base::Value& callback_id = args[0];
  ResolveJavascriptCallback(callback_id, BuildShortcutList());
}

void RoamuxShortcutsHandler::HandleRebindShortcut(const base::ListValue& args) {
  AllowJavascript();
  const base::Value& callback_id = args[0];
  const std::string* pref_key = args[1].GetIfString();
  const base::DictValue* chord_dict = args[2].GetIfDict();
  std::string error = "invalid";
  if (pref_key && chord_dict) {
    error = Rebind(*pref_key, *chord_dict);
  }
  ResolveJavascriptCallback(callback_id, base::Value(error));
}

base::ListValue RoamuxShortcutsHandler::BuildShortcutList() {
  Profile* profile = Profile::FromWebUI(web_ui());
  base::ListValue list;
  for (const tabs::RoamuxShortcut* entry :
       tabs::EnabledShortcuts(tabs::AllShortcuts())) {
    const tabs::Chord chord =
        tabs::GetCurrentChord(profile->GetPrefs(), *entry);
    base::DictValue item;
    item.Set("key", entry->pref_key);
    item.Set("label", entry->label);
    item.Set("chordText", ChordDisplayString(chord));
    list.Append(std::move(item));
  }
  return list;
}

std::string RoamuxShortcutsHandler::Rebind(const std::string& pref_key,
                                           const base::DictValue& chord_dict) {
  Profile* profile = Profile::FromWebUI(web_ui());
  // Wire format from the recorder: modifier booleans + the DOM `code` string;
  // mapped into the Carbon domain here (Step-8 finding 1).
  const std::string* code = chord_dict.FindString("code");
  std::optional<bool> cmd = chord_dict.FindBool("cmd");
  std::optional<bool> shift = chord_dict.FindBool("shift");
  std::optional<bool> ctrl = chord_dict.FindBool("ctrl");
  std::optional<bool> opt = chord_dict.FindBool("opt");
  if (!code || !cmd || !shift || !ctrl || !opt) {
    return "invalid";
  }
  tabs::Chord chord_value;
  chord_value.cmd = *cmd;
  chord_value.shift = *shift;
  chord_value.ctrl = *ctrl;
  chord_value.opt = *opt;
  chord_value.keycode = tabs::CarbonKeycodeFromDomCodeString(*code);
  if (chord_value.keycode < 0) {
    return "invalid";
  }
  std::optional<tabs::Chord> chord = chord_value;
  for (const tabs::RoamuxShortcut* entry :
       tabs::EnabledShortcuts(tabs::AllShortcuts())) {
    if (pref_key != entry->pref_key) {
      continue;
    }
    const std::vector<tabs::Chord> reserved =
        tabs::EnumerateReservedChords(entry->command_id);
    switch (tabs::ValidateRebind(profile->GetPrefs(), tabs::AllShortcuts(),
                                 *entry, *chord, reserved)) {
      case tabs::RebindResult::kOk:
        tabs::StoreRebind(profile->GetPrefs(), *entry, *chord);
        return "";
      case tabs::RebindResult::kInvalid:
        return "invalid";
      case tabs::RebindResult::kConflictsReserved:
        return "reserved";
      case tabs::RebindResult::kConflictsRoamux:
        return "roamux";
    }
  }
  return "invalid";
}

}  // namespace roamux

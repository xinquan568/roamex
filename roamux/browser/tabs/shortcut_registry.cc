// SPDX-License-Identifier: Apache-2.0
#include "roamux/browser/tabs/shortcut_registry.h"

#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "roamux/common/roamux_features.h"
#include "roamux/common/roamux_prefs.h"

namespace roamux::tabs {

namespace {

// Keep in sync with chrome/app/chrome_command_ids.h (patch 0010).
constexpr int kIdcReloadInitialUrl = 34059;
// Keep in sync with chrome/app/chrome_command_ids.h (roam-25 patch 0017).
constexpr int kIdcTabVisitBack = 33010;
constexpr int kIdcTabVisitForward = 33011;
// Keep in sync with chrome/app/chrome_command_ids.h (roam-214 patch 0053).
constexpr int kIdcToggleTabStrip = 33012;
// kVK_ANSI_R.
constexpr int kVkAnsiR = 0x0F;
// kVK_ANSI_LeftBracket / kVK_ANSI_RightBracket.
constexpr int kVkAnsiLeftBracket = 0x21;
constexpr int kVkAnsiRightBracket = 0x1E;
// kVK_ANSI_T.
constexpr int kVkAnsiT = 0x11;

constexpr RoamuxShortcut kShortcuts[] = {
    {kIdcReloadInitialUrl,
     "reload_initial_url",
     "Reload initial URL",
     &features::kInitialUrl,
     {.cmd = true,
      .shift = false,
      .ctrl = true,
      .opt = false,
      .keycode = kVkAnsiR}},
    // roam-25 (I-4.5): E4 tab visit-order navigation. Ctrl+Cmd+[ / Ctrl+Cmd+].
    {kIdcTabVisitBack,
     "tab_visit_back",
     "Tab visit back",
     &features::kTabVisitNav,
     {.cmd = true,
      .shift = false,
      .ctrl = true,
      .opt = false,
      .keycode = kVkAnsiLeftBracket}},
    {kIdcTabVisitForward,
     "tab_visit_forward",
     "Tab visit forward",
     &features::kTabVisitNav,
     {.cmd = true,
      .shift = false,
      .ctrl = true,
      .opt = false,
      .keycode = kVkAnsiRightBracket}},
    // roam-214: pin/peek toggle for the vertical tab strip. Ctrl+Cmd+T.
    {kIdcToggleTabStrip,
     "toggle_tab_strip",
     "Toggle tab strip",
     &features::kTabStripToggleShortcut,
     {.cmd = true,
      .shift = false,
      .ctrl = true,
      .opt = false,
      .keycode = kVkAnsiT}},
};

}  // namespace

base::DictValue Chord::ToDict() const {
  base::DictValue dict;
  dict.Set("cmd", cmd);
  dict.Set("shift", shift);
  dict.Set("ctrl", ctrl);
  dict.Set("opt", opt);
  dict.Set("keycode", keycode);
  return dict;
}

// static
std::optional<Chord> Chord::FromDict(const base::DictValue& dict) {
  Chord chord;
  std::optional<bool> cmd = dict.FindBool("cmd");
  std::optional<bool> shift = dict.FindBool("shift");
  std::optional<bool> ctrl = dict.FindBool("ctrl");
  std::optional<bool> opt = dict.FindBool("opt");
  std::optional<int> keycode = dict.FindInt("keycode");
  if (!cmd || !shift || !ctrl || !opt || !keycode) {
    return std::nullopt;
  }
  chord.cmd = *cmd;
  chord.shift = *shift;
  chord.ctrl = *ctrl;
  chord.opt = *opt;
  chord.keycode = *keycode;
  return chord;
}

base::span<const RoamuxShortcut> AllShortcuts() {
  return kShortcuts;
}

std::vector<const RoamuxShortcut*> EnabledShortcuts(
    base::span<const RoamuxShortcut> table) {
  std::vector<const RoamuxShortcut*> enabled;
  for (const RoamuxShortcut& entry : table) {
    if (base::FeatureList::IsEnabled(*entry.feature)) {
      enabled.push_back(&entry);
    }
  }
  return enabled;
}

Chord GetCurrentChord(const PrefService* prefs, const RoamuxShortcut& entry) {
  if (prefs) {
    const base::DictValue& bindings = prefs->GetDict(prefs::kShortcutBindings);
    if (const base::DictValue* stored = bindings.FindDict(entry.pref_key)) {
      if (std::optional<Chord> chord = Chord::FromDict(*stored)) {
        return *chord;
      }
    }
  }
  return entry.default_chord;
}

RebindResult ValidateRebind(const PrefService* prefs,
                            base::span<const RoamuxShortcut> table,
                            const RoamuxShortcut& entry,
                            const Chord& chord,
                            base::span<const Chord> reserved) {
  // A chord needs a plausible Carbon keycode (0 IS a key: kVK_ANSI_A) and at
  // least one non-shift modifier.
  if (chord.keycode < 0 || chord.keycode > 0x7F ||
      !(chord.cmd || chord.ctrl || chord.opt)) {
    return RebindResult::kInvalid;
  }
  for (const Chord& taken : reserved) {
    if (taken == chord) {
      return RebindResult::kConflictsReserved;
    }
  }
  for (const RoamuxShortcut* other : EnabledShortcuts(table)) {
    if (other->command_id != entry.command_id &&
        GetCurrentChord(prefs, *other) == chord) {
      return RebindResult::kConflictsRoamux;
    }
  }
  return RebindResult::kOk;
}

void StoreRebind(PrefService* prefs,
                 const RoamuxShortcut& entry,
                 const Chord& chord) {
  ScopedDictPrefUpdate update(prefs, prefs::kShortcutBindings);
  update->Set(entry.pref_key, chord.ToDict());
}

int CommandForChord(const PrefService* prefs,
                    base::span<const RoamuxShortcut> table,
                    const Chord& chord) {
  for (const RoamuxShortcut* entry : EnabledShortcuts(table)) {
    if (GetCurrentChord(prefs, *entry) == chord) {
      return entry->command_id;
    }
  }
  return -1;
}

}  // namespace roamux::tabs

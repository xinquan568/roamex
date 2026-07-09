// SPDX-License-Identifier: Apache-2.0
#include "roamex/browser/tabs/shortcut_registry.h"

#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "roamex/common/roamex_features.h"
#include "roamex/common/roamex_prefs.h"

namespace roamex::tabs {

namespace {

// Keep in sync with chrome/app/chrome_command_ids.h (patch 0010).
constexpr int kIdcReloadInitialUrl = 34059;
// Keep in sync with chrome/app/chrome_command_ids.h (roam-25 patch 0017).
constexpr int kIdcTabVisitBack = 33010;
constexpr int kIdcTabVisitForward = 33011;
// kVK_ANSI_R.
constexpr int kVkAnsiR = 0x0F;
// kVK_ANSI_LeftBracket / kVK_ANSI_RightBracket.
constexpr int kVkAnsiLeftBracket = 0x21;
constexpr int kVkAnsiRightBracket = 0x1E;

constexpr RoamexShortcut kShortcuts[] = {
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

base::span<const RoamexShortcut> AllShortcuts() {
  return kShortcuts;
}

std::vector<const RoamexShortcut*> EnabledShortcuts(
    base::span<const RoamexShortcut> table) {
  std::vector<const RoamexShortcut*> enabled;
  for (const RoamexShortcut& entry : table) {
    if (base::FeatureList::IsEnabled(*entry.feature)) {
      enabled.push_back(&entry);
    }
  }
  return enabled;
}

Chord GetCurrentChord(const PrefService* prefs, const RoamexShortcut& entry) {
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
                            base::span<const RoamexShortcut> table,
                            const RoamexShortcut& entry,
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
  for (const RoamexShortcut* other : EnabledShortcuts(table)) {
    if (other->command_id != entry.command_id &&
        GetCurrentChord(prefs, *other) == chord) {
      return RebindResult::kConflictsRoamex;
    }
  }
  return RebindResult::kOk;
}

void StoreRebind(PrefService* prefs,
                 const RoamexShortcut& entry,
                 const Chord& chord) {
  ScopedDictPrefUpdate update(prefs, prefs::kShortcutBindings);
  update->Set(entry.pref_key, chord.ToDict());
}

int CommandForChord(const PrefService* prefs,
                    base::span<const RoamexShortcut> table,
                    const Chord& chord) {
  for (const RoamexShortcut* entry : EnabledShortcuts(table)) {
    if (GetCurrentChord(prefs, *entry) == chord) {
      return entry->command_id;
    }
  }
  return -1;
}

}  // namespace roamex::tabs

// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_BROWSER_TABS_SHORTCUT_REGISTRY_H_
#define ROAMUX_BROWSER_TABS_SHORTCUT_REGISTRY_H_

#include <optional>
#include <string>
#include <vector>

#include "base/containers/span.h"
#include "base/feature_list.h"
#include "base/memory/raw_ptr_exclusion.h"
#include "base/values.h"

class PrefService;

namespace roamux::tabs {

// A key chord (macOS semantics). Serialized into the roamux.shortcuts dict.
struct Chord {
  bool cmd = false;
  bool shift = false;
  bool ctrl = false;
  bool opt = false;
  int keycode = 0;  // Carbon virtual keycode (kVK_*).

  bool operator==(const Chord&) const = default;
  base::DictValue ToDict() const;
  static std::optional<Chord> FromDict(const base::DictValue& dict);
};

// One Roamux-owned rebindable command (§4.3). Each entry gates on its OWN
// feature — E4 entries add a table row and nothing else changes.
struct RoamuxShortcut {
  int command_id;
  const char* pref_key;  // Key inside roamux.shortcuts.
  const char* label;     // v1 literal English (D5 precedent).
  // Entries live in a constexpr static table (never heap), so raw_ptr does
  // not apply.
  RAW_PTR_EXCLUSION const base::Feature* feature;
  Chord default_chord;
};

enum class RebindResult {
  kOk,
  kInvalid,            // Empty/modifier-less chord.
  kConflictsReserved,  // Collides with a browser accelerator / menu chord.
  kConflictsRoamux,    // Collides with another enabled roamux binding.
};

// The production table.
base::span<const RoamuxShortcut> AllShortcuts();

// Entries whose feature is enabled. All consumers (listing, dispatch,
// validation) filter through this.
std::vector<const RoamuxShortcut*> EnabledShortcuts(
    base::span<const RoamuxShortcut> table);

// The effective chord: pref override else default.
Chord GetCurrentChord(const PrefService* prefs, const RoamuxShortcut& entry);

// §4.3 conflict rule. `reserved` is the runtime-enumerable reserved set
// (accelerator table + non-menu shortcuts + app-menu key equivalents),
// supplied by the caller (mac-side enumeration lives in the handler/.mm).
RebindResult ValidateRebind(const PrefService* prefs,
                            base::span<const RoamuxShortcut> table,
                            const RoamuxShortcut& entry,
                            const Chord& chord,
                            base::span<const Chord> reserved);

// Persists an override (call after ValidateRebind returns kOk).
void StoreRebind(PrefService* prefs,
                 const RoamuxShortcut& entry,
                 const Chord& chord);

// Resolves a chord against the ENABLED entries' current bindings; returns the
// command id or -1.
int CommandForChord(const PrefService* prefs,
                    base::span<const RoamuxShortcut> table,
                    const Chord& chord);

}  // namespace roamux::tabs

#endif  // ROAMUX_BROWSER_TABS_SHORTCUT_REGISTRY_H_

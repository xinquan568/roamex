// SPDX-License-Identifier: Apache-2.0
// roam-13 (I-2.4): the shortcut registry — serialization, default/override,
// the §4.3 conflict truth table, per-entry feature gating, and the E4-shape
// proof (a fake second entry participates purely by table row).

#include "roamux/browser/tabs/shortcut_registry.h"

#include "base/test/scoped_feature_list.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "roamux/common/roamux_features.h"
#include "roamux/common/roamux_prefs.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace roamux::tabs {
namespace {

BASE_FEATURE(kFakeE4Feature,
             "RoamuxFakeE4Feature",
             base::FEATURE_DISABLED_BY_DEFAULT);

constexpr Chord kCtrlCmdR{.cmd = true, .ctrl = true, .keycode = 0x0F};
constexpr Chord kCtrlCmdT{.cmd = true, .ctrl = true, .keycode = 0x11};
constexpr Chord kCtrlCmdY{.cmd = true, .ctrl = true, .keycode = 0x10};

constexpr RoamuxShortcut kTestTable[] = {
    {34059, "reload_initial_url", "Reload initial URL", &features::kInitialUrl,
     kCtrlCmdR},
    // The E4-shape proof: one added row, its own feature, nothing else.
    {90001, "fake_e4_command", "Fake E4", &kFakeE4Feature, kCtrlCmdT},
};

class ShortcutRegistryTest : public testing::Test {
 protected:
  ShortcutRegistryTest() {
    prefs::RegisterProfilePrefs(pref_service_.registry());
  }

  TestingPrefServiceSimple pref_service_;
};

TEST_F(ShortcutRegistryTest, ChordSerializationRoundTrips) {
  const Chord chord{
      .cmd = true, .shift = true, .ctrl = false, .opt = true, .keycode = 42};
  std::optional<Chord> parsed = Chord::FromDict(chord.ToDict());
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(chord, *parsed);
  EXPECT_FALSE(Chord::FromDict(base::DictValue()).has_value());
}

TEST_F(ShortcutRegistryTest, CurrentChordIsDefaultThenOverride) {
  base::test::ScopedFeatureList features;
  features.InitAndEnableFeature(features::kInitialUrl);
  const RoamuxShortcut& entry = kTestTable[0];
  EXPECT_EQ(kCtrlCmdR, GetCurrentChord(&pref_service_, entry));
  StoreRebind(&pref_service_, entry, kCtrlCmdY);
  EXPECT_EQ(kCtrlCmdY, GetCurrentChord(&pref_service_, entry));
}

TEST_F(ShortcutRegistryTest, PerEntryFeatureGating) {
  base::test::ScopedFeatureList features;
  features.InitWithFeatures({features::kInitialUrl}, {kFakeE4Feature});
  auto enabled = EnabledShortcuts(kTestTable);
  ASSERT_EQ(1u, enabled.size());
  EXPECT_EQ(34059, enabled[0]->command_id);
  // The disabled entry neither dispatches nor conflicts.
  EXPECT_EQ(-1, CommandForChord(&pref_service_, kTestTable, kCtrlCmdT));
  EXPECT_EQ(RebindResult::kOk, ValidateRebind(&pref_service_, kTestTable,
                                              kTestTable[0], kCtrlCmdT, {}));
}

TEST_F(ShortcutRegistryTest, FakeE4EntryParticipatesByTableRowAlone) {
  base::test::ScopedFeatureList features;
  features.InitWithFeatures({features::kInitialUrl, kFakeE4Feature}, {});
  auto enabled = EnabledShortcuts(kTestTable);
  ASSERT_EQ(2u, enabled.size());
  EXPECT_EQ(90001, CommandForChord(&pref_service_, kTestTable, kCtrlCmdT));
  // Cross-entry conflict now applies.
  EXPECT_EQ(
      RebindResult::kConflictsRoamux,
      ValidateRebind(&pref_service_, kTestTable, kTestTable[0], kCtrlCmdT, {}));
}

TEST_F(ShortcutRegistryTest, ValidateRebindTruthTable) {
  base::test::ScopedFeatureList features;
  features.InitAndEnableFeature(features::kInitialUrl);
  const RoamuxShortcut& entry = kTestTable[0];
  // Invalid: out-of-range keycode / shift-only. Keycode 0 IS a key
  // (kVK_ANSI_A) and must validate.
  EXPECT_EQ(RebindResult::kInvalid,
            ValidateRebind(&pref_service_, kTestTable, entry,
                           Chord{.cmd = true, .keycode = -1}, {}));
  EXPECT_EQ(RebindResult::kInvalid,
            ValidateRebind(&pref_service_, kTestTable, entry,
                           Chord{.cmd = true, .keycode = 0x200}, {}));
  EXPECT_EQ(RebindResult::kInvalid,
            ValidateRebind(&pref_service_, kTestTable, entry,
                           Chord{.shift = true, .keycode = 5}, {}));
  EXPECT_EQ(RebindResult::kOk,
            ValidateRebind(&pref_service_, kTestTable, entry,
                           Chord{.cmd = true, .opt = true, .keycode = 0}, {}))
      << "Ctrl/Cmd+A (Carbon keycode 0) is a valid chord";
  // Reserved conflict.
  const Chord reserved[] = {kCtrlCmdY};
  EXPECT_EQ(
      RebindResult::kConflictsReserved,
      ValidateRebind(&pref_service_, kTestTable, entry, kCtrlCmdY, reserved));
  // OK.
  EXPECT_EQ(RebindResult::kOk,
            ValidateRebind(&pref_service_, kTestTable, entry,
                           Chord{.cmd = true, .opt = true, .keycode = 9}, {}));
}

TEST_F(ShortcutRegistryTest, DispatchHonorsOverride) {
  base::test::ScopedFeatureList features;
  features.InitAndEnableFeature(features::kInitialUrl);
  const RoamuxShortcut& entry = kTestTable[0];
  EXPECT_EQ(34059, CommandForChord(&pref_service_, kTestTable, kCtrlCmdR));
  StoreRebind(&pref_service_, entry, kCtrlCmdY);
  EXPECT_EQ(-1, CommandForChord(&pref_service_, kTestTable, kCtrlCmdR))
      << "the old chord must stop dispatching";
  EXPECT_EQ(34059, CommandForChord(&pref_service_, kTestTable, kCtrlCmdY));
}

TEST_F(ShortcutRegistryTest, ProductionTableShape) {
  // The tripwire: every production-table change must update this consciously.
  // Row 0 = roam-12 reload-initial-url; rows 1-2 = roam-25 tab-visit
  // traversal (Ctrl+Cmd+[ / Ctrl+Cmd+], Carbon keycodes 0x21 / 0x1E);
  // row 3 = roam-214 tab-strip pin/peek toggle (Ctrl+Cmd+T, 0x11).
  constexpr Chord kCtrlCmdLeftBracket{
      .cmd = true, .ctrl = true, .keycode = 0x21};
  constexpr Chord kCtrlCmdRightBracket{
      .cmd = true, .ctrl = true, .keycode = 0x1E};
  // kCtrlCmdT is the file-scope constant (0x11) — also the roam-214 default.

  auto table = AllShortcuts();
  ASSERT_EQ(4u, table.size());

  EXPECT_EQ(34059, table[0].command_id);
  EXPECT_STREQ("reload_initial_url", table[0].pref_key);
  EXPECT_EQ(&features::kInitialUrl, table[0].feature);
  EXPECT_EQ(kCtrlCmdR, table[0].default_chord);

  EXPECT_EQ(33010, table[1].command_id);
  EXPECT_STREQ("tab_visit_back", table[1].pref_key);
  EXPECT_EQ(&features::kTabVisitNav, table[1].feature);
  EXPECT_EQ(kCtrlCmdLeftBracket, table[1].default_chord);

  EXPECT_EQ(33011, table[2].command_id);
  EXPECT_STREQ("tab_visit_forward", table[2].pref_key);
  EXPECT_EQ(&features::kTabVisitNav, table[2].feature);
  EXPECT_EQ(kCtrlCmdRightBracket, table[2].default_chord);

  EXPECT_EQ(33012, table[3].command_id);
  EXPECT_STREQ("toggle_tab_strip", table[3].pref_key);
  EXPECT_EQ(&features::kTabStripToggleShortcut, table[3].feature);
  EXPECT_EQ(kCtrlCmdT, table[3].default_chord);
}

}  // namespace
}  // namespace roamux::tabs

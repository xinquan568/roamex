// SPDX-License-Identifier: Apache-2.0
// roam-214: the pin/peek state machine (TDD — RED against the S1 stubs).
// Covers the D3 arming table, the D4 predicate truth table, explicit-unpin
// paths, promote-from-hover, the suppression lifecycle, Mode-A/Mode-B
// delegate-effect split, overlapping/own-lock exclusion, and D9 resets
// including the omnibox lock-release ordering and region-detach teardown.

#include "roamux/browser/ui/views/tabs/tab_strip_pin_controller.h"

#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace roamux {
namespace {

class FakeDelegate : public TabStripPinController::Delegate {
 public:
  void AcquireKeepExpanded() override {
    ++keep_expanded_held_;
    calls_.push_back("acquire_keep");
  }
  void ReleaseKeepExpanded() override {
    if (keep_expanded_held_ > 0) {
      --keep_expanded_held_;
      calls_.push_back("release_keep");
    }
  }
  void AcquireForceCollapse() override {
    ++force_collapse_held_;
    calls_.push_back("acquire_suppress");
  }
  void ReleaseForceCollapse() override {
    if (force_collapse_held_ > 0) {
      --force_collapse_held_;
      calls_.push_back("release_suppress");
    }
  }
  int ExternalKeepOpenLockCount() override { return external_locks_; }
  bool IsPointerInsideStrip() override { return pointer_inside_; }
  bool IsFocusInsideStrip() override { return focus_inside_; }
  void FocusStrip() override { calls_.push_back("focus_strip"); }
  void ForceCollapseNow() override { calls_.push_back("force_collapse"); }
  void ToggleCollapseViaButtonSemantics() override {
    calls_.push_back("mode_b_toggle");
  }

  int keep_expanded_held_ = 0;
  int force_collapse_held_ = 0;
  int external_locks_ = 0;
  bool pointer_inside_ = true;  // pin usually starts with pointer on strip
  bool focus_inside_ = false;
  std::vector<std::string> calls_;
};

class TabStripPinControllerTest : public testing::Test {
 protected:
  TabStripPinControllerTest() : controller_(&fake_) {}

  void Pin() {
    controller_.OnShortcut(TabStripPinController::Mode::kPinPeek);
    ASSERT_TRUE(controller_.pinned());
  }

  // Arms and moves pointer+focus outside, so only the caller-set conditions
  // block the predicate.
  void ArmAndLeave() {
    controller_.OnCompletedInteraction();
    fake_.pointer_inside_ = false;
    fake_.focus_inside_ = false;
  }

  FakeDelegate fake_;
  TabStripPinController controller_;
};

// --- Pinning (D1) ---

TEST_F(TabStripPinControllerTest, ShortcutPinsAndFocusesStrip) {
  controller_.OnShortcut(TabStripPinController::Mode::kPinPeek);
  EXPECT_TRUE(controller_.pinned());
  EXPECT_FALSE(controller_.armed());
  EXPECT_EQ(fake_.keep_expanded_held_, 1);
  EXPECT_EQ(fake_.calls_.front(), "acquire_keep");
  EXPECT_NE(std::find(fake_.calls_.begin(), fake_.calls_.end(), "focus_strip"),
            fake_.calls_.end());
}

TEST_F(TabStripPinControllerTest, PromoteFromHoverIsJustPin) {
  // Hover-revealed is not a distinct core state: not-pinned + shortcut = pin
  // (D1 promotion) regardless of current visual expansion.
  fake_.pointer_inside_ = true;
  Pin();
  EXPECT_EQ(fake_.keep_expanded_held_, 1);
}

// --- Explicit unpin paths ---

TEST_F(TabStripPinControllerTest,
       ShortcutWhilePinnedUnpinsImmediatelyAndSuppressesUnderCursor) {
  Pin();
  fake_.pointer_inside_ = true;
  controller_.OnShortcut(TabStripPinController::Mode::kPinPeek);
  EXPECT_FALSE(controller_.pinned());
  EXPECT_TRUE(controller_.suppressing());
  EXPECT_EQ(fake_.keep_expanded_held_, 0);
  EXPECT_EQ(fake_.force_collapse_held_, 1);
  EXPECT_NE(
      std::find(fake_.calls_.begin(), fake_.calls_.end(), "force_collapse"),
      fake_.calls_.end());
}

TEST_F(TabStripPinControllerTest, ShortcutUnpinWithPointerOutsideNoSuppress) {
  Pin();
  fake_.pointer_inside_ = false;
  controller_.OnShortcut(TabStripPinController::Mode::kPinPeek);
  EXPECT_FALSE(controller_.pinned());
  EXPECT_FALSE(controller_.suppressing());
  EXPECT_EQ(fake_.force_collapse_held_, 0);
}

TEST_F(TabStripPinControllerTest, EscInStripUnpinsImmediately) {
  Pin();
  fake_.pointer_inside_ = true;
  controller_.OnEscInStrip();
  EXPECT_FALSE(controller_.pinned());
  EXPECT_TRUE(controller_.suppressing());
}

TEST_F(TabStripPinControllerTest, EscWhenNotPinnedIsNoOp) {
  controller_.OnEscInStrip();
  EXPECT_TRUE(fake_.calls_.empty());
}

TEST_F(TabStripPinControllerTest, SuppressionReleasedOnPointerExit) {
  Pin();
  fake_.pointer_inside_ = true;
  controller_.OnShortcut(TabStripPinController::Mode::kPinPeek);
  ASSERT_TRUE(controller_.suppressing());
  controller_.OnPointerExit();
  EXPECT_FALSE(controller_.suppressing());
  EXPECT_EQ(fake_.force_collapse_held_, 0);
}

// --- Arming (D3): the not-arming rows are enforced at the reporter layer
// (only completed interactions call OnCompletedInteraction), so the core
// contract is: completion arms, and nothing else does. ---

TEST_F(TabStripPinControllerTest, CompletedInteractionArms) {
  Pin();
  controller_.OnCompletedInteraction();
  EXPECT_TRUE(controller_.armed());
  EXPECT_TRUE(controller_.pinned());  // arming alone never unpins
}

TEST_F(TabStripPinControllerTest, CompletedInteractionWhenNotPinnedIsNoOp) {
  controller_.OnCompletedInteraction();
  EXPECT_FALSE(controller_.armed());
}

TEST_F(TabStripPinControllerTest, PointerExitAloneDoesNotUnpinUnarmed) {
  Pin();
  fake_.pointer_inside_ = false;
  fake_.focus_inside_ = false;
  controller_.OnPointerExit();
  EXPECT_TRUE(controller_.pinned());  // looked, didn't act
}

// --- The D4 predicate: armed && !pointer && !focus && no external lock ---

// The full 16-combination truth table (plan WB-2): the pin dissolves iff
// armed AND pointer outside AND focus outside AND no external keep-open
// lock; every other combination stays pinned.
struct D4Row {
  bool armed;
  bool pointer_inside;
  bool focus_inside;
  bool external_lock;
};

class TabStripPinControllerD4Test : public testing::TestWithParam<int> {};

TEST_P(TabStripPinControllerD4Test, TruthTable) {
  const int bits = GetParam();
  const D4Row row{(bits & 8) != 0, (bits & 4) != 0, (bits & 2) != 0,
                  (bits & 1) != 0};
  FakeDelegate fake;
  TabStripPinController controller(&fake);
  fake.pointer_inside_ = true;
  controller.OnShortcut(TabStripPinController::Mode::kPinPeek);
  ASSERT_TRUE(controller.pinned());
  if (row.armed) {
    controller.OnCompletedInteraction();
  }
  fake.pointer_inside_ = row.pointer_inside;
  fake.focus_inside_ = row.focus_inside;
  fake.external_locks_ = row.external_lock ? 1 : 0;
  // Re-evaluate through every predicate trigger.
  controller.OnPointerExit();
  controller.OnFocusChanged();
  controller.OnExternalLockReleased();
  const bool should_unpin = row.armed && !row.pointer_inside &&
                            !row.focus_inside && !row.external_lock;
  EXPECT_EQ(controller.pinned(), !should_unpin)
      << "armed=" << row.armed << " ptr=" << row.pointer_inside
      << " focus=" << row.focus_inside << " lock=" << row.external_lock;
}

INSTANTIATE_TEST_SUITE_P(AllCombinations,
                         TabStripPinControllerD4Test,
                         testing::Range(0, 16));

TEST_F(TabStripPinControllerTest, PredicateUnpinsOnPointerExit) {
  Pin();
  ArmAndLeave();
  controller_.OnPointerExit();
  EXPECT_FALSE(controller_.pinned());
  EXPECT_EQ(fake_.keep_expanded_held_, 0);
  // Predicate unpin releases the lock; it does NOT force-collapse (upstream
  // animates naturally).
  EXPECT_EQ(
      std::count(fake_.calls_.begin(), fake_.calls_.end(), "force_collapse"),
      0);
}

TEST_F(TabStripPinControllerTest, PredicateBlockedByPointerInside) {
  Pin();
  controller_.OnCompletedInteraction();
  fake_.pointer_inside_ = true;
  fake_.focus_inside_ = false;
  controller_.OnFocusChanged();
  EXPECT_TRUE(controller_.pinned());  // serial closes: pointer never left
}

TEST_F(TabStripPinControllerTest, PredicateBlockedByFocusInside) {
  Pin();
  controller_.OnCompletedInteraction();
  fake_.pointer_inside_ = false;
  fake_.focus_inside_ = true;
  controller_.OnPointerExit();
  EXPECT_TRUE(controller_.pinned());
}

TEST_F(TabStripPinControllerTest, PredicateBlockedByExternalLock) {
  Pin();
  ArmAndLeave();
  fake_.external_locks_ = 1;  // context menu / drag holds a keep-open lock
  controller_.OnPointerExit();
  EXPECT_TRUE(controller_.pinned());
}

TEST_F(TabStripPinControllerTest, OwnLockNeverBlocksPredicate) {
  // ExternalKeepOpenLockCount excludes our own kKeepExpanded by contract;
  // the fake models that exclusion (external_locks_ stays 0 while our pin
  // lock is held) — the predicate must fire.
  Pin();
  ASSERT_EQ(fake_.keep_expanded_held_, 1);
  ArmAndLeave();
  controller_.OnPointerExit();
  EXPECT_FALSE(controller_.pinned());
}

TEST_F(TabStripPinControllerTest, LockReleaseReevaluatesPredicate) {
  // D9 omnibox ordering: menu/omnibox lock released -> predicate runs
  // BEFORE any re-expand; if it holds, unpin instead of bouncing open.
  Pin();
  ArmAndLeave();
  fake_.external_locks_ = 1;
  controller_.OnPointerExit();
  ASSERT_TRUE(controller_.pinned());
  fake_.external_locks_ = 0;
  controller_.OnExternalLockReleased();
  EXPECT_FALSE(controller_.pinned());
}

TEST_F(TabStripPinControllerTest, ReArmingAcrossSerialCloses) {
  // Close several tabs in a row: each completion re-arms, pointer stays ->
  // pinned until the pointer finally leaves.
  Pin();
  controller_.OnCompletedInteraction();
  controller_.OnCompletedInteraction();
  EXPECT_TRUE(controller_.pinned());
  fake_.pointer_inside_ = false;
  fake_.focus_inside_ = false;
  controller_.OnPointerExit();
  EXPECT_FALSE(controller_.pinned());
}

// --- Mode B (D7): pure pass-through to the button semantics ---

TEST_F(TabStripPinControllerTest, PlainToggleUsesButtonSemanticsOnly) {
  controller_.OnShortcut(TabStripPinController::Mode::kPlainToggle);
  EXPECT_FALSE(controller_.pinned());
  EXPECT_EQ(fake_.calls_, std::vector<std::string>({"mode_b_toggle"}));
}

TEST_F(TabStripPinControllerTest, DisabledModeIsNoOp) {
  controller_.OnShortcut(TabStripPinController::Mode::kDisabled);
  EXPECT_TRUE(fake_.calls_.empty());
}

// --- D9 lifecycle resets + F4 teardown ---

TEST_F(TabStripPinControllerTest, LifecycleResetDropsEverything) {
  Pin();
  controller_.OnCompletedInteraction();
  controller_.OnLifecycleReset();
  EXPECT_FALSE(controller_.pinned());
  EXPECT_FALSE(controller_.armed());
  EXPECT_FALSE(controller_.suppressing());
  EXPECT_EQ(fake_.keep_expanded_held_, 0);
  EXPECT_EQ(fake_.force_collapse_held_, 0);
  // Reset never force-collapses (upstream owns the visuals on mode change).
  EXPECT_EQ(
      std::count(fake_.calls_.begin(), fake_.calls_.end(), "force_collapse"),
      0);
}

TEST_F(TabStripPinControllerTest, RegionDetachDropsLocksSafely) {
  Pin();
  fake_.pointer_inside_ = true;
  controller_.OnShortcut(TabStripPinController::Mode::kPinPeek);  // suppress
  Pin();  // pin again on top of suppression -> suppression must clear
  controller_.OnRegionDetached();
  EXPECT_FALSE(controller_.pinned());
  EXPECT_FALSE(controller_.suppressing());
  EXPECT_EQ(fake_.keep_expanded_held_, 0);
  EXPECT_EQ(fake_.force_collapse_held_, 0);
}

TEST_F(TabStripPinControllerTest, RePinClearsSuppression) {
  Pin();
  fake_.pointer_inside_ = true;
  controller_.OnShortcut(TabStripPinController::Mode::kPinPeek);
  ASSERT_TRUE(controller_.suppressing());
  controller_.OnShortcut(TabStripPinController::Mode::kPinPeek);
  EXPECT_TRUE(controller_.pinned());
  EXPECT_FALSE(controller_.suppressing());
  EXPECT_EQ(fake_.force_collapse_held_, 0);
  EXPECT_EQ(fake_.keep_expanded_held_, 1);
}

}  // namespace
}  // namespace roamux

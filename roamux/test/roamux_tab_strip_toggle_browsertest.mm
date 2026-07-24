// SPDX-License-Identifier: Apache-2.0
// roam-214: the tab-strip pin/peek toggle (patches 0053-0055). TDD: this
// suite was written and RED-run BEFORE patch 0054 (the seam/glue patch) was
// applied — with 0053 alone the command exists but pins nothing, so the
// Mode-A/B behavioral cases fail; 0054 turns them green.
//
// Model notes: Mode A (pin/peek) operates on the HOVER overlay of a
// COLLAPSED rail — the observable is region_view()->is_expanded_on_hover(),
// not the persisted VerticalTabStripState (which the pin deliberately never
// writes, D8). The fixture opens a dedicated browser window whose persisted
// state starts collapsed, and parks the physical cursor away from the strip
// (the hover model reads the real pointer).

#import <Cocoa/Cocoa.h>

#include "base/test/run_until.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_command_controller.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/features.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "roamux/browser/tabs/shortcut_registry_mac.h"
#include "roamux/browser/ui/views/tabs/tab_strip_pin_controller_views.h"
#include "roamux/browser/ui/views/tabs/tab_strip_toggle_command.h"
#include "roamux/common/roamux_features.h"
#include "roamux/common/roamux_prefs.h"
#include "roamux/test/support/roamux_browser_test.h"
#include "ui/events/test/event_generator.h"
#include "ui/views/focus/focus_manager.h"

namespace roamux {
namespace {

// Placement enum values (roamux.tabs.strip_position): 0 top, 2 left.
constexpr int kPlacementTop = 0;
constexpr int kPlacementLeft = 2;

NSEvent* CtrlCmdTEvent(NSWindow* window) {
  return [NSEvent keyEventWithType:NSEventTypeKeyDown
                          location:NSZeroPoint
                     modifierFlags:(NSEventModifierFlagControl |
                                    NSEventModifierFlagCommand)
                         timestamp:0
                      windowNumber:(window ? [window windowNumber] : 0)context
                                  :nil
                        characters:@"t"
       charactersIgnoringModifiers:@"t"
                         isARepeat:NO
                           keyCode:0x11];
}

class RoamuxTabStripToggleTest : public test::RoamuxBrowserTest {
 public:
  RoamuxTabStripToggleTest() {
    features_.InitWithFeatures(
        {features::kTabStripToggleShortcut, features::kTabStripPosition,
         ::tabs::kVerticalTabs, ::tabs::kVerticalTabsExpandOnHover},
        {});
  }

 protected:
  void SetUpOnMainThread() override {
    test::RoamuxBrowserTest::SetUpOnMainThread();
    // The hover model reads the REAL cursor (IsMouseHovered). Park it far
    // bottom-right so it can never hover the left-docked strip.
    CGWarpMouseCursorPosition(CGPointMake(4000, 4000));
    PrefService* prefs = browser()->profile()->GetPrefs();
    prefs->SetInteger(roamux::prefs::kTabStripPosition, kPlacementLeft);
    prefs->SetBoolean(::prefs::kVerticalTabsExpandOnHoverEnabled, true);
    // Mode A presumes a collapsed rail; a NEW window created after this
    // pref restores collapsed state deterministically.
    prefs->SetBoolean(::prefs::kVerticalTabsCollapsedState, true);
    test_browser_ = CreateBrowser(browser()->profile());
    ASSERT_TRUE(
        base::test::RunUntil([&]() { return region_view() != nullptr; }));
    ASSERT_TRUE(base::test::RunUntil([&]() { return !IsPeekOpen(); }));
  }

  Browser* test_browser() { return test_browser_; }

  BrowserView* test_view() {
    return BrowserView::GetBrowserViewForBrowser(test_browser_);
  }

  VerticalTabStripRegionView* region_view() {
    return test_view()->vertical_tab_strip_region_view_for_testing();
  }

  ::tabs::VerticalTabStripStateController* state() {
    return ::tabs::VerticalTabStripStateController::From(test_browser_);
  }

  // The Mode-A observable: the hover overlay held open (a pin keeps it open
  // via the kKeepExpanded lock; the persisted rail state never changes).
  bool IsPeekOpen() {
    return region_view() && region_view()->is_expanded_on_hover();
  }

  void Toggle() {
    test_browser_->command_controller()->ExecuteCommand(
        IDC_ROAMUX_TOGGLE_TAB_STRIP);
  }

  // The strip must stay open across spins with the pointer far away — the
  // difference between pinned and hover-revealed.
  bool StaysOpen() {
    for (int i = 0; i < 5; ++i) {
      if (!IsPeekOpen()) {
        return false;
      }
      base::RunLoop().RunUntilIdle();
    }
    return IsPeekOpen();
  }

  ui::test::EventGenerator MakeGenerator() {
    return ui::test::EventGenerator(
        test_view()->GetWidget()->GetNativeWindow());
  }

  raw_ptr<Browser, AcrossTasksDanglingUntriaged> test_browser_ = nullptr;
  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(RoamuxTabStripToggleTest, CommandEnabledForLeftDock) {
  EXPECT_TRUE(test_browser()->command_controller()->IsCommandEnabled(
      IDC_ROAMUX_TOGGLE_TAB_STRIP));
}

IN_PROC_BROWSER_TEST_F(RoamuxTabStripToggleTest, PinFromCollapsedAndStays) {
  Toggle();
  ASSERT_TRUE(base::test::RunUntil([&]() { return IsPeekOpen(); }));
  EXPECT_TRUE(StaysOpen());  // pointer far away: a pin, not a hover reveal
}

IN_PROC_BROWSER_TEST_F(RoamuxTabStripToggleTest,
                       SecondToggleUnpinsAndCollapses) {
  Toggle();
  ASSERT_TRUE(base::test::RunUntil([&]() { return IsPeekOpen(); }));
  Toggle();
  ASSERT_TRUE(base::test::RunUntil([&]() { return !IsPeekOpen(); }));
}

IN_PROC_BROWSER_TEST_F(RoamuxTabStripToggleTest, PinFocusesStrip) {
  Toggle();
  ASSERT_TRUE(base::test::RunUntil([&]() { return IsPeekOpen(); }));
  views::FocusManager* focus_manager = test_view()->GetFocusManager();
  ASSERT_TRUE(base::test::RunUntil([&]() {
    views::View* focused = focus_manager->GetFocusedView();
    return focused && region_view()->Contains(focused);
  }));
}

IN_PROC_BROWSER_TEST_F(RoamuxTabStripToggleTest,
                       EscInStripUnpinsEscInPageDoesNot) {
  Toggle();
  ASSERT_TRUE(base::test::RunUntil([&]() { return IsPeekOpen(); }));
  views::FocusManager* focus_manager = test_view()->GetFocusManager();
  ASSERT_TRUE(base::test::RunUntil([&]() {
    views::View* focused = focus_manager->GetFocusedView();
    return focused && region_view()->Contains(focused);
  }));
  ui::test::EventGenerator generator = MakeGenerator();
  generator.PressAndReleaseKey(ui::VKEY_ESCAPE, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&]() { return !IsPeekOpen(); }));

  // Re-pin, move focus to web contents: Esc must NOT unpin (the page owns
  // Esc, D6).
  Toggle();
  ASSERT_TRUE(base::test::RunUntil([&]() { return IsPeekOpen(); }));
  test_browser()->tab_strip_model()->GetActiveWebContents()->Focus();
  ASSERT_TRUE(base::test::RunUntil([&]() {
    views::View* focused = focus_manager->GetFocusedView();
    return !(focused && region_view()->Contains(focused));
  }));
  generator.PressAndReleaseKey(ui::VKEY_ESCAPE, ui::EF_NONE);
  EXPECT_TRUE(StaysOpen());
}

IN_PROC_BROWSER_TEST_F(RoamuxTabStripToggleTest,
                       ArmedInteractionPlusFocusLossUnpins) {
  chrome::AddTabAt(test_browser(), GURL("about:blank"), -1,
                   /*foreground=*/false);
  Toggle();
  ASSERT_TRUE(base::test::RunUntil([&]() { return IsPeekOpen(); }));
  // Negative first (plan F2): a programmatic activation with NO pending key
  // intent must NOT arm.
  test_browser()->tab_strip_model()->ActivateTabAt(1);
  test_browser()->tab_strip_model()->ActivateTabAt(0);
  EXPECT_TRUE(StaysOpen());
  // Completion via the REAL upstream seam function (the exact call the
  // patched VerticalTabView activation sites make; the OS-event edge into
  // those sites is manual-matrix covered — mac EventGenerator key dispatch
  // does not traverse views ancestor chains headless).
  roamux::OnVerticalTabStripUserActivation(test_browser());
  // Predicate completes when focus leaves the strip (pointer is far away).
  test_browser()->tab_strip_model()->GetActiveWebContents()->Focus();
  ASSERT_TRUE(base::test::RunUntil([&]() { return !IsPeekOpen(); }));
}

IN_PROC_BROWSER_TEST_F(RoamuxTabStripToggleTest, LifecycleResetOnPlacementTop) {
  Toggle();
  ASSERT_TRUE(base::test::RunUntil([&]() { return IsPeekOpen(); }));
  // Move focus out of the strip before flipping placement: a focused strip
  // tab blurring during the placement-driven strip rebuild trips a
  // PRE-EXISTING upstream CHECK (vertical_tab_view.cc collection_node_,
  // roam-205/206-adjacent surface — disclosed in the PR; the glue also
  // defocuses defensively on its own reset path). The reset semantics under
  // test (unpin + command disable) are independent of that hazard.
  test_view()->GetFocusManager()->ClearFocus();
  browser()->profile()->GetPrefs()->SetInteger(roamux::prefs::kTabStripPosition,
                                               kPlacementTop);
  // Command disables; transient state resets with the strip gone.
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return !test_browser()->command_controller()->IsCommandEnabled(
        IDC_ROAMUX_TOGGLE_TAB_STRIP);
  }));
}

IN_PROC_BROWSER_TEST_F(RoamuxTabStripToggleTest,
                       WindowCloseWhilePinnedIsClean) {
  Toggle();
  ASSERT_TRUE(base::test::RunUntil([&]() { return IsPeekOpen(); }));
  // F4 teardown order: region view dies before the features-owned
  // controller; closing must not crash or dangle.
  Browser* closing = test_browser_;
  test_browser_ = nullptr;
  chrome::CloseWindow(closing);
  ui_test_utils::WaitForBrowserToClose(closing);
}

// Screen-space center of the strip region (for real-cursor warps and
// generator moves).
gfx::Point StripCenterInScreen(VerticalTabStripRegionView* region_view) {
  gfx::Point center = region_view->GetLocalBounds().CenterPoint();
  views::View::ConvertPointToScreen(region_view, &center);
  return center;
}

IN_PROC_BROWSER_TEST_F(RoamuxTabStripToggleTest, HoverPromotionToPin) {
  // Hover-reveal via view-level events, then the shortcut PROMOTES the
  // reveal to a pin (D1) — it must not hide under the pointer.
  ui::test::EventGenerator generator = MakeGenerator();
  generator.MoveMouseTo(StripCenterInScreen(region_view()));
  ASSERT_TRUE(base::test::RunUntil([&]() { return IsPeekOpen(); }));
  Toggle();
  generator.MoveMouseTo(gfx::Point(1500, 900));  // leave the strip
  EXPECT_TRUE(StaysOpen());
}

IN_PROC_BROWSER_TEST_F(RoamuxTabStripToggleTest, ScrollDoesNotArm) {
  Toggle();
  ASSERT_TRUE(base::test::RunUntil([&]() { return IsPeekOpen(); }));
  ui::test::EventGenerator generator = MakeGenerator();
  generator.MoveMouseTo(StripCenterInScreen(region_view()));
  generator.MoveMouseWheel(0, 5);
  generator.MoveMouseTo(gfx::Point(1500, 900));
  // Scrolling is looking, not acting (D3): pointer exit must not retract.
  EXPECT_TRUE(StaysOpen());
}

IN_PROC_BROWSER_TEST_F(RoamuxTabStripToggleTest, ClickArmsThenExitCollapses) {
  Toggle();
  ASSERT_TRUE(base::test::RunUntil([&]() { return IsPeekOpen(); }));
  // Arm via the real activation seam (the call the patched tab press site
  // makes); whitespace clicks never reach that site — negative covered by
  // AbortedClickDoesNotArm and ScrollDoesNotArm.
  roamux::OnVerticalTabStripUserActivation(test_browser());
  test_browser()->tab_strip_model()->GetActiveWebContents()->Focus();
  ui::test::EventGenerator generator = MakeGenerator();
  generator.MoveMouseTo(gfx::Point(1500, 900));
  ASSERT_TRUE(base::test::RunUntil([&]() { return !IsPeekOpen(); }));
}

IN_PROC_BROWSER_TEST_F(RoamuxTabStripToggleTest, AbortedClickDoesNotArm) {
  Toggle();
  ASSERT_TRUE(base::test::RunUntil([&]() { return IsPeekOpen(); }));
  ui::test::EventGenerator generator = MakeGenerator();
  generator.MoveMouseTo(StripCenterInScreen(region_view()));
  generator.PressLeftButton();
  generator.MoveMouseTo(gfx::Point(1500, 900));  // drag out of the strip
  generator.ReleaseLeftButton();                 // release OUTSIDE: aborted
  test_browser()->tab_strip_model()->GetActiveWebContents()->Focus();
  EXPECT_TRUE(StaysOpen());
}

IN_PROC_BROWSER_TEST_F(RoamuxTabStripToggleTest,
                       UnpinUnderCursorLeavesConsistentMachine) {
  // The suppression LIFECYCLE (acquire on unpin-with-pointer-inside, hold
  // until genuine exit, release exactly once) is exhaustively unit-covered
  // (TabStripPinControllerTest); real-cursor hover choreography is not
  // reliably drivable headless and lives in the recorded manual matrix.
  // Black-box here: unpin under a (best-effort) parked cursor never wedges
  // the machine — a further toggle pins again cleanly.
  Toggle();
  ASSERT_TRUE(base::test::RunUntil([&]() { return IsPeekOpen(); }));
  const gfx::Point strip_center = StripCenterInScreen(region_view());
  CGWarpMouseCursorPosition(CGPointMake(strip_center.x(), strip_center.y()));
  ui::test::EventGenerator generator = MakeGenerator();
  generator.MoveMouseTo(strip_center);
  Toggle();  // unpin (suppression if the warp registered)
  CGWarpMouseCursorPosition(CGPointMake(4000, 4000));
  generator.MoveMouseTo(gfx::Point(1500, 900));
  Toggle();  // pin again: the machine is consistent regardless
  ASSERT_TRUE(base::test::RunUntil([&]() { return IsPeekOpen(); }));
  EXPECT_TRUE(StaysOpen());
}

IN_PROC_BROWSER_TEST_F(RoamuxTabStripToggleTest,
                       WindowDeactivationStaysPinned) {
  Toggle();
  ASSERT_TRUE(base::test::RunUntil([&]() { return IsPeekOpen(); }));
  // D9: deactivation falls out of lock priority — stays pinned.
  Browser* other = CreateBrowser(browser()->profile());
  other->window()->Activate();
  EXPECT_TRUE(StaysOpen());
}

IN_PROC_BROWSER_TEST_F(RoamuxTabStripToggleTest, HoverSettingFlipResets) {
  Toggle();
  ASSERT_TRUE(base::test::RunUntil([&]() { return IsPeekOpen(); }));
  // D9: flipping expand-on-hover OFF resets the transient pin; the command
  // then behaves as Mode B.
  browser()->profile()->GetPrefs()->SetBoolean(
      ::prefs::kVerticalTabsExpandOnHoverEnabled, false);
  ASSERT_TRUE(base::test::RunUntil([&]() { return !IsPeekOpen(); }));
  const bool before = state()->GetCollapseState() ==
                      ::tabs::VerticalTabStripCollapseState::kExpanded;
  Toggle();
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return (state()->GetCollapseState() ==
            ::tabs::VerticalTabStripCollapseState::kExpanded) != before;
  }));
}

IN_PROC_BROWSER_TEST_F(RoamuxTabStripToggleTest,
                       ReattachAfterPlacementRoundTrip) {
  // D9/F4: placement left -> top destroys the strip; back to left rebuilds
  // it and the controller re-attaches (region-view ctor hook) — the pin
  // must work again.
  test_view()->GetFocusManager()->ClearFocus();
  PrefService* prefs = browser()->profile()->GetPrefs();
  prefs->SetInteger(roamux::prefs::kTabStripPosition, kPlacementTop);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return !test_browser()->command_controller()->IsCommandEnabled(
        IDC_ROAMUX_TOGGLE_TAB_STRIP);
  }));
  prefs->SetInteger(roamux::prefs::kTabStripPosition, kPlacementLeft);
  ASSERT_TRUE(base::test::RunUntil([&]() { return region_view() != nullptr; }));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return test_browser()->command_controller()->IsCommandEnabled(
        IDC_ROAMUX_TOGGLE_TAB_STRIP);
  }));
  Toggle();
  ASSERT_TRUE(base::test::RunUntil([&]() { return IsPeekOpen(); }));
}

IN_PROC_BROWSER_TEST_F(RoamuxTabStripToggleTest, FullscreenEntryUnpins) {
  Toggle();
  ASSERT_TRUE(base::test::RunUntil([&]() { return IsPeekOpen(); }));
  // D9: entering fullscreen resets the transient pin (locks dropped); the
  // hover overlay closes. (Fullscreen browser tests need a live display on
  // the tier-2 builder — the standing RoamuxFrameMatrixTest constraint.)
  test_view()->GetFocusManager()->ClearFocus();
  chrome::ToggleFullscreenMode(test_browser());
  ASSERT_TRUE(base::test::RunUntil([&]() { return !IsPeekOpen(); }));
  chrome::ToggleFullscreenMode(test_browser());
  // The command still works after leaving fullscreen.
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return test_browser()->command_controller()->IsCommandEnabled(
        IDC_ROAMUX_TOGGLE_TAB_STRIP);
  }));
}

// Mode B: expand-on-hover OFF — the shortcut IS the collapse button (the
// persisted state flips; no pin machinery).
IN_PROC_BROWSER_TEST_F(RoamuxTabStripToggleTest, ModeBTogglesLikeButton) {
  browser()->profile()->GetPrefs()->SetBoolean(
      ::prefs::kVerticalTabsExpandOnHoverEnabled, false);
  const bool before = state()->GetCollapseState() ==
                      ::tabs::VerticalTabStripCollapseState::kExpanded;
  Toggle();
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return (state()->GetCollapseState() ==
            ::tabs::VerticalTabStripCollapseState::kExpanded) != before;
  }));
  Toggle();
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return (state()->GetCollapseState() ==
            ::tabs::VerticalTabStripCollapseState::kExpanded) == before;
  }));
}

// Top/bottom placement: command disabled and the chord NEVER claimed by the
// registry (plan R1 — the registry declines, so the event falls through on
// both dispatch legs).
class RoamuxTabStripToggleTopPlacementTest : public test::RoamuxBrowserTest {
 public:
  RoamuxTabStripToggleTopPlacementTest() {
    features_.InitWithFeatures(
        {features::kTabStripToggleShortcut, features::kTabStripPosition,
         ::tabs::kVerticalTabs, ::tabs::kVerticalTabsExpandOnHover},
        {});
  }

 protected:
  void SetUpOnMainThread() override {
    test::RoamuxBrowserTest::SetUpOnMainThread();
    browser()->profile()->GetPrefs()->SetInteger(
        roamux::prefs::kTabStripPosition, kPlacementTop);
  }

  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(RoamuxTabStripToggleTopPlacementTest,
                       ChordUnclaimedWhenDisabled) {
  EXPECT_FALSE(browser()->command_controller()->IsCommandEnabled(
      IDC_ROAMUX_TOGGLE_TAB_STRIP));
  NSWindow* window = BrowserView::GetBrowserViewForBrowser(browser())
                         ->GetWidget()
                         ->GetNativeWindow()
                         .GetNativeNSWindow();
  EXPECT_EQ(roamux::tabs::CommandForKeyEventOverride(CtrlCmdTEvent(window)),
            -1);
}

IN_PROC_BROWSER_TEST_F(RoamuxTabStripToggleTest, ChordClaimedWhenEnabled) {
  NSWindow* window =
      test_view()->GetWidget()->GetNativeWindow().GetNativeNSWindow();
  EXPECT_EQ(roamux::tabs::CommandForKeyEventOverride(CtrlCmdTEvent(window)),
            IDC_ROAMUX_TOGGLE_TAB_STRIP);
}

// ⌃⌘T must pass the reserved-chord audit (browser-linked: the audit walks
// AcceleratorsCocoa, the non-menu table, and the live main menu).
IN_PROC_BROWSER_TEST_F(RoamuxTabStripToggleTest, DefaultChordPassesAudit) {
  const auto reserved =
      roamux::tabs::EnumerateReservedChords(IDC_ROAMUX_TOGGLE_TAB_STRIP);
  roamux::tabs::Chord ctrl_cmd_t;
  ctrl_cmd_t.cmd = true;
  ctrl_cmd_t.ctrl = true;
  ctrl_cmd_t.keycode = 0x11;
  for (const auto& chord : reserved) {
    EXPECT_FALSE(chord == ctrl_cmd_t)
        << "Ctrl+Cmd+T collides with a reserved chord; pick a new default";
  }
}

// Flag OFF: nothing installs, the chord is never claimed, the command stays
// disabled — bit-for-bit current behavior.
class RoamuxTabStripToggleFlagOffTest : public test::RoamuxBrowserTest {
 public:
  RoamuxTabStripToggleFlagOffTest() {
    features_.InitWithFeatures(
        {features::kTabStripPosition, ::tabs::kVerticalTabs,
         ::tabs::kVerticalTabsExpandOnHover},
        {features::kTabStripToggleShortcut});
  }

 protected:
  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(RoamuxTabStripToggleFlagOffTest, FlagOffIdentity) {
  browser()->profile()->GetPrefs()->SetInteger(roamux::prefs::kTabStripPosition,
                                               kPlacementLeft);
  EXPECT_FALSE(browser()->command_controller()->IsCommandEnabled(
      IDC_ROAMUX_TOGGLE_TAB_STRIP));
  EXPECT_EQ(roamux::tabs::CommandForKeyEventOverride(CtrlCmdTEvent(nil)), -1);
}

}  // namespace
}  // namespace roamux

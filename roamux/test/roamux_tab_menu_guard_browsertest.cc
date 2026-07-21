// SPDX-License-Identifier: Apache-2.0
// roam-181: the roamux tab-menu submenu anchor (command 2101) is an item of
// the parent TabMenuModel, whose delegate is upstream TabContextMenuController.
// Without a range guard, its blind static_cast to
// TabStripModel::ContextMenuCommand reaches fatal NOTREACHED()/CHECK paths in
// TabStripModel, and MenuControllerCocoa's eager IsEnabledAt sweep while
// building the native menu crashed the browser on a bare right-click. These
// tests pair the REAL TabMenuModel with the REAL TabContextMenuController —
// the configuration the null-delegate host in
// tab_strip_position_menu_unittest.cc cannot reach.

#include "base/test/scoped_feature_list.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/tabs/tab_menu_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tabs/browser_tab_strip_controller.h"
#include "chrome/browser/ui/views/tabs/tab/tab_context_menu_controller.h"
#include "chrome/browser/ui/views/tabs/tab_strip.h"
#include "content/public/test/browser_test.h"
#include "roamux/browser/ui/tabs/initial_url_menu.h"
#include "roamux/browser/ui/tabs/tab_strip_position_menu.h"
#include "roamux/common/roamux_features.h"
#include "roamux/test/support/roamux_browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/accelerators/accelerator.h"

namespace roamux::test {
namespace {

// The production Delegate is BrowserTabStripController; BrowserView installs
// exactly that type on its TabStrip, so the downcast holds in a browser test.
BrowserTabStripController* GetProductionDelegate(Browser* browser) {
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser);
  if (!browser_view || !browser_view->horizontal_tab_strip_for_testing()) {
    return nullptr;
  }
  return static_cast<BrowserTabStripController*>(
      browser_view->horizontal_tab_strip_for_testing()->controller());
}

// Records every Delegate call; under strict mode a call is itself a failure
// (guarded ids must never reach the delegate). The positive control runs with
// strict off so an upstream id can be recorded without failing the test.
class RecordingDelegate : public TabContextMenuController::Delegate {
 public:
  bool IsContextMenuCommandChecked(
      TabStripModel::ContextMenuCommand command_id) override {
    RecordCall("IsContextMenuCommandChecked");
    return false;
  }
  bool IsContextMenuCommandEnabled(
      int index,
      TabStripModel::ContextMenuCommand command_id) override {
    RecordCall("IsContextMenuCommandEnabled");
    return false;
  }
  bool IsContextMenuCommandAlerted(
      TabStripModel::ContextMenuCommand command_id) override {
    RecordCall("IsContextMenuCommandAlerted");
    return false;
  }
  void ExecuteContextMenuCommand(int index,
                                 TabStripModel::ContextMenuCommand command_id,
                                 int event_flags) override {
    RecordCall("ExecuteContextMenuCommand");
  }
  bool GetContextMenuAccelerator(int command_id,
                                 ui::Accelerator* accelerator) override {
    RecordCall("GetContextMenuAccelerator");
    return false;
  }

  void set_strict(bool strict) { strict_ = strict; }
  int call_count() const { return call_count_; }

 private:
  void RecordCall(const char* method) {
    ++call_count_;
    if (strict_) {
      ADD_FAILURE() << "guarded roamux id reached the upstream delegate via "
                    << method;
    }
  }

  bool strict_ = true;
  int call_count_ = 0;
};

class RoamuxTabMenuGuardTest : public RoamuxBrowserTest {
 public:
  RoamuxTabMenuGuardTest() {
    feature_list_.InitAndEnableFeature(roamux::features::kTabStripPosition);
  }

 private:
  base::test::ScopedFeatureList feature_list_;
};

// roam-185 flips the compiled default later in this epic; inertness must pin
// the flag off explicitly rather than rely on the default.
class RoamuxTabMenuGuardFlagOffTest : public RoamuxBrowserTest {
 public:
  RoamuxTabMenuGuardFlagOffTest() {
    feature_list_.InitAndDisableFeature(roamux::features::kTabStripPosition);
  }

 private:
  base::test::ScopedFeatureList feature_list_;
};

// The crash regression: the exact production pairing from
// BrowserTabStripController::ShowContextMenuForTab, queried the way
// MenuControllerCocoa queries it while building the native menu. Pre-guard
// this dies on the roamux anchor row (NOTREACHED in
// TabStripModel::IsContextMenuCommandEnabled).
IN_PROC_BROWSER_TEST_F(RoamuxTabMenuGuardTest,
                       ProductionDelegatePairSurvivesFullEnabledSweep) {
  BrowserTabStripController* const controller =
      GetProductionDelegate(browser());
  ASSERT_TRUE(controller);
  TabContextMenuController context_menu_controller(0, controller);
  TabMenuModel model(&context_menu_controller,
                     browser()->GetFeatures().tab_menu_model_delegate(),
                     browser()->tab_strip_model(), 0);

  bool saw_roamux_anchor = false;
  for (size_t i = 0; i < model.GetItemCount(); ++i) {
    if (model.GetCommandIdAt(i) == tabs::kTabStripPositionSubMenuCommandId) {
      saw_roamux_anchor = true;
    }
    model.IsEnabledAt(i);
    model.IsItemCheckedAt(i);
  }
  EXPECT_TRUE(saw_roamux_anchor);
}

// The guard itself: every roamux id short-circuits with a safe default on all
// five delegate methods and never touches the upstream delegate; a genuine
// upstream id still routes through (the guard is range-scoped, not blanket).
IN_PROC_BROWSER_TEST_F(RoamuxTabMenuGuardTest,
                       GuardShortCircuitsAllFiveDelegateMethods) {
  RecordingDelegate fake;
  TabContextMenuController controller(0, &fake);

  for (int id = tabs::kTabStripPositionSubMenuCommandId;
       id <= tabs::kTabStripPositionFirstItemCommandId + 3; ++id) {
    EXPECT_TRUE(controller.IsCommandIdEnabled(id));
    EXPECT_FALSE(controller.IsCommandIdChecked(id));
    EXPECT_FALSE(controller.IsCommandIdAlerted(id));
    ui::Accelerator accelerator;
    EXPECT_FALSE(controller.GetAcceleratorForCommandId(id, &accelerator));
    controller.ExecuteCommand(id, /*event_flags=*/0);
  }
  EXPECT_EQ(0, fake.call_count());

  fake.set_strict(false);
  EXPECT_FALSE(controller.IsCommandIdChecked(TabStripModel::CommandCloseTab));
  EXPECT_EQ(1, fake.call_count());
}

// roam-194: the Initial URL submenu (roam-14, patch 0012, ids 2110-2112) has
// the same anchor-in-parent-model topology as roam-181's 2101. Its fixture
// enables kInitialUrl — which no other fixture in this file does — while
// kTabStripPosition keeps its shipped-on default (roam-185), so both roamux
// anchors coexist in the model, as they will for users once roam-187 lands.
class RoamuxInitialUrlMenuGuardTest : public RoamuxBrowserTest {
 public:
  RoamuxInitialUrlMenuGuardTest() {
    feature_list_.InitAndEnableFeature(roamux::features::kInitialUrl);
  }

 private:
  base::test::ScopedFeatureList feature_list_;
};

// The roam-194 crash regression: with RoamuxInitialUrl on, the 2110 anchor
// sits in the parent TabMenuModel and MenuControllerCocoa's eager IsEnabledAt
// sweep queries it on a bare right-click. Pre-guard this dies on the 2110 row
// (NOTREACHED in TabStripModel::IsContextMenuCommandEnabled) exactly as
// roam-181's 2101 did.
IN_PROC_BROWSER_TEST_F(RoamuxInitialUrlMenuGuardTest,
                       ProductionDelegatePairSurvivesInitialUrlSweep) {
  BrowserTabStripController* const controller =
      GetProductionDelegate(browser());
  ASSERT_TRUE(controller);
  TabContextMenuController context_menu_controller(0, controller);
  TabMenuModel model(&context_menu_controller,
                     browser()->GetFeatures().tab_menu_model_delegate(),
                     browser()->tab_strip_model(), 0);

  // Assert on the 2110 anchor specifically — the 2101 anchor is also present
  // (kTabStripPosition ships enabled) and must not be conflated with it.
  bool saw_initial_url_anchor = false;
  for (size_t i = 0; i < model.GetItemCount(); ++i) {
    if (model.GetCommandIdAt(i) == tabs::kInitialUrlSubMenuCommandId) {
      saw_initial_url_anchor = true;
    }
    model.IsEnabledAt(i);
    model.IsItemCheckedAt(i);
  }
  EXPECT_TRUE(saw_initial_url_anchor);
}

// The guard for the initial-url range: strict direct checks over the roamux
// ids ONLY (upstream rows legitimately route to the delegate, so a strict
// full-row sweep would over-fail); the upstream passthrough control proves
// the widened guard is range-scoped, not blanket.
IN_PROC_BROWSER_TEST_F(RoamuxInitialUrlMenuGuardTest,
                       GuardShortCircuitsInitialUrlIds) {
  RecordingDelegate fake;
  TabContextMenuController controller(0, &fake);

  for (int id = tabs::kInitialUrlSubMenuCommandId;
       id <= tabs::kSetInitialUrlToCurrentPageCommandId; ++id) {
    EXPECT_TRUE(controller.IsCommandIdEnabled(id));
    EXPECT_FALSE(controller.IsCommandIdChecked(id));
    EXPECT_FALSE(controller.IsCommandIdAlerted(id));
    ui::Accelerator accelerator;
    EXPECT_FALSE(controller.GetAcceleratorForCommandId(id, &accelerator));
    controller.ExecuteCommand(id, /*event_flags=*/0);
  }
  EXPECT_EQ(0, fake.call_count());

  fake.set_strict(false);
  EXPECT_FALSE(controller.IsCommandIdChecked(TabStripModel::CommandCloseTab));
  EXPECT_EQ(1, fake.call_count());
}

// Flag-off inertness to its verifiable extent: no row carries a roamux
// command id, and the full production-pair query sweep is crash-free.
IN_PROC_BROWSER_TEST_F(RoamuxTabMenuGuardFlagOffTest, FlagOffMenuIsStock) {
  BrowserTabStripController* const controller =
      GetProductionDelegate(browser());
  ASSERT_TRUE(controller);
  TabContextMenuController context_menu_controller(0, controller);
  TabMenuModel model(&context_menu_controller,
                     browser()->GetFeatures().tab_menu_model_delegate(),
                     browser()->tab_strip_model(), 0);

  for (size_t i = 0; i < model.GetItemCount(); ++i) {
    EXPECT_FALSE(tabs::IsTabStripPositionCommandId(model.GetCommandIdAt(i)));
    model.IsEnabledAt(i);
    model.IsItemCheckedAt(i);
  }
}

}  // namespace
}  // namespace roamux::test

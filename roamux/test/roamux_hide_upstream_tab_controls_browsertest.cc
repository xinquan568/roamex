// SPDX-License-Identifier: Apache-2.0
// roam-183: with RoamuxTabStripPosition on, the upstream vertical/horizontal
// tab-position controls must be suppressed everywhere (the roamux placement is
// the sole authority since roam-182). This covers the three menu surfaces — the
// tab context menu (TabStripModel::CommandToggleVertical), the app Tools menu
// and the system/window menu (IDC_TOGGLE_VERTICAL_TABS) — with a flag-off
// control proving the guard is roamux-flag-scoped, not a blanket removal. The
// Appearance settings row is covered in
// roamux_tab_strip_position_settings_browsertest.cc.

#include "base/test/scoped_feature_list.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/tabs/features.h"
#include "chrome/browser/ui/tabs/tab_menu_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/toolbar/app_menu_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/browser_widget.h"
#include "chrome/browser/ui/views/tabs/browser_tab_strip_controller.h"
#include "chrome/browser/ui/views/tabs/tab/tab_context_menu_controller.h"
#include "chrome/browser/ui/views/tabs/tab_strip.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "roamux/browser/ui/tabs/tab_strip_position_menu.h"
#include "roamux/common/roamux_features.h"
#include "roamux/common/roamux_prefs.h"
#include "roamux/test/support/roamux_browser_test.h"
#include "ui/base/models/menu_model.h"
#include "ui/menus/simple_menu_model.h"

namespace roamux {
namespace {

// True if `model` (or any of its submenus) exposes `command_id`.
bool MenuHasCommand(const ui::MenuModel* model, int command_id) {
  for (size_t i = 0; i < model->GetItemCount(); ++i) {
    if (model->GetTypeAt(i) != ui::MenuModel::TYPE_SEPARATOR &&
        model->GetCommandIdAt(i) == command_id) {
      return true;
    }
    if (model->GetTypeAt(i) == ui::MenuModel::TYPE_SUBMENU) {
      if (const ui::MenuModel* sub = model->GetSubmenuModelAt(i);
          sub && MenuHasCommand(sub, command_id)) {
        return true;
      }
    }
  }
  return false;
}

// A no-op delegate so ToolsMenuModel can be built outside the full
// AppMenuModel.
class StubMenuDelegate : public ui::SimpleMenuModel::Delegate {
 public:
  bool IsCommandIdChecked(int) const override { return false; }
  bool IsCommandIdEnabled(int) const override { return true; }
  void ExecuteCommand(int, int) override {}
};

BrowserTabStripController* ProductionTabStripController(Browser* browser) {
  BrowserView* const view = BrowserView::GetBrowserViewForBrowser(browser);
  if (!view || !view->horizontal_tab_strip_for_testing()) {
    return nullptr;
  }
  return static_cast<BrowserTabStripController*>(
      view->horizontal_tab_strip_for_testing()->controller());
}

// Flag ON + upstream vertical-tabs feature ON: the upstream controls would
// otherwise show; roam-183 must suppress them while the roamux placement is
// Left/Right (vertical displayed).
class RoamuxHideUpstreamMenusTest : public roamux::test::RoamuxBrowserTest {
 public:
  RoamuxHideUpstreamMenusTest() {
    features_.InitWithFeatures(
        {features::kTabStripPosition, ::tabs::kVerticalTabs}, {});
  }

 protected:
  void SetLeftPlacement() {
    browser()->profile()->GetPrefs()->SetInteger(prefs::kTabStripPosition, 2);
  }

  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(RoamuxHideUpstreamMenusTest,
                       TabContextMenuHasNoUpstreamToggle) {
  SetLeftPlacement();
  BrowserTabStripController* const controller =
      ProductionTabStripController(browser());
  ASSERT_TRUE(controller);
  TabContextMenuController ctx(0, controller);
  TabMenuModel model(&ctx, browser()->GetFeatures().tab_menu_model_delegate(),
                     browser()->tab_strip_model(), 0);

  EXPECT_FALSE(MenuHasCommand(&model, TabStripModel::CommandToggleVertical));
  // The roamux submenu anchor (roam-6/181) must still be present.
  EXPECT_TRUE(MenuHasCommand(&model, tabs::kTabStripPositionSubMenuCommandId));
}

IN_PROC_BROWSER_TEST_F(RoamuxHideUpstreamMenusTest,
                       AppToolsMenuHasNoUpstreamToggle) {
  SetLeftPlacement();
  StubMenuDelegate delegate;
  ToolsMenuModel tools(&delegate, browser());
  EXPECT_FALSE(MenuHasCommand(&tools, IDC_TOGGLE_VERTICAL_TABS));
}

IN_PROC_BROWSER_TEST_F(RoamuxHideUpstreamMenusTest,
                       SystemMenuHasNoUpstreamVerticalTabsGroup) {
  SetLeftPlacement();
  ui::MenuModel* const menu = BrowserView::GetBrowserViewForBrowser(browser())
                                  ->browser_widget()
                                  ->GetSystemMenuModel();
  ASSERT_TRUE(menu);
  EXPECT_FALSE(MenuHasCommand(menu, IDC_TOGGLE_VERTICAL_TABS));
  EXPECT_FALSE(MenuHasCommand(menu, IDC_TOGGLE_VERTICAL_TABS_EXPAND_ON_HOVER));
}

// Flag OFF + upstream vertical-tabs feature ON: stock behavior — each surface
// still exposes its toggle (proves the guard is roamux-flag-scoped).
class RoamuxHideUpstreamMenusFlagOffTest
    : public roamux::test::RoamuxBrowserTest {
 public:
  RoamuxHideUpstreamMenusFlagOffTest() {
    features_.InitWithFeatures({::tabs::kVerticalTabs},
                               {features::kTabStripPosition});
  }

 protected:
  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(RoamuxHideUpstreamMenusFlagOffTest,
                       UpstreamTogglesPresentStock) {
  // Upstream vertical tabs displayed via the upstream pref (roamux flag off).
  browser()->profile()->GetPrefs()->SetBoolean(::prefs::kVerticalTabsEnabled,
                                               true);
  StubMenuDelegate delegate;
  ToolsMenuModel tools(&delegate, browser());
  EXPECT_TRUE(MenuHasCommand(&tools, IDC_TOGGLE_VERTICAL_TABS));

  ui::MenuModel* const menu = BrowserView::GetBrowserViewForBrowser(browser())
                                  ->browser_widget()
                                  ->GetSystemMenuModel();
  ASSERT_TRUE(menu);
  EXPECT_TRUE(MenuHasCommand(menu, IDC_TOGGLE_VERTICAL_TABS));

  BrowserTabStripController* const controller =
      ProductionTabStripController(browser());
  ASSERT_TRUE(controller);
  TabContextMenuController ctx(0, controller);
  TabMenuModel model(&ctx, browser()->GetFeatures().tab_menu_model_delegate(),
                     browser()->tab_strip_model(), 0);
  EXPECT_TRUE(MenuHasCommand(&model, TabStripModel::CommandToggleVertical));
}

}  // namespace
}  // namespace roamux

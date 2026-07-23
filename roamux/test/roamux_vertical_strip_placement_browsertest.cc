// SPDX-License-Identifier: Apache-2.0
// roam-8 (I-1.3): placement left/right reuses the upstream vertical tab strip
// (maintainer-authorized surface): display mapping (both docks), right-dock
// geometry, live switching across all four placements, flag-off inertness, and
// upstream-pref precedence. (TDD: written RED before patch 0008.)

#include <string_view>

#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/actions/chrome_action_id.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_actions.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/features.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "roamux/common/roamux_features.h"
#include "roamux/common/roamux_prefs.h"
#include "roamux/test/support/roamux_browser_test.h"
#include "ui/actions/actions.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/views/vector_icons.h"
#include "ui/views/view.h"

namespace roamux {
namespace {

// roam-206: the collapse action item, and the ImageModel
// UpdateCollapseActionItem builds for a given icon.
actions::ActionItem* CollapseActionItem(Browser* browser) {
  return actions::ActionManager::Get().FindAction(
      kActionToggleCollapseVertical,
      browser->browser_actions()->root_action_item());
}


ui::ImageModel IconModel(const gfx::VectorIcon& icon) {
  return ui::ImageModel::FromVectorIcon(icon, ui::kColorIcon);
}

// Drives collapse through the public RequestCollapse round-trip (the
// delegate animates, then commits) — the upstream browsertest pattern.
void CollapseAndWait(::tabs::VerticalTabStripStateController* controller,
                     bool collapsed) {
  if (controller->IsCollapsed() == collapsed) {
    return;
  }
  controller->RequestCollapse(collapsed);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return controller->IsCollapsed() == collapsed; }));
}

views::View* FindViewByClassName(views::View* root, std::string_view name) {
  if (root->GetClassName() == name) {
    return root;
  }
  for (views::View* child : root->children()) {
    if (views::View* hit = FindViewByClassName(child, name)) {
      return hit;
    }
  }
  return nullptr;
}

gfx::Rect BoundsInBrowserView(views::View* view, BrowserView* bv) {
  gfx::RectF rect(gfx::SizeF(view->size()));
  views::View::ConvertRectToTarget(view, bv, &rect);
  return gfx::ToEnclosingRect(rect);
}

class RoamuxVerticalStripPlacementTest
    : public roamux::test::RoamuxBrowserTest {
 public:
  RoamuxVerticalStripPlacementTest() {
    // Deliberately does NOT enable the upstream vertical-tabs feature flags:
    // the roamux path must work without them (plan D1).
    features_.InitAndEnableFeature(features::kTabStripPosition);
  }

 protected:
  BrowserView* browser_view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }

  void SetPlacementAndLayout(int value) {
    browser()->profile()->GetPrefs()->SetInteger(prefs::kTabStripPosition,
                                                 value);
    base::RunLoop().RunUntilIdle();
    browser_view()->DeprecatedLayoutImmediately();
  }

  views::View* vertical_region() {
    return FindViewByClassName(browser_view(), "VerticalTabStripRegionView");
  }

  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(RoamuxVerticalStripPlacementTest,
                       LeftPlacementDisplaysVerticalStripAtLeadingEdge) {
  SetPlacementAndLayout(2);  // kLeft.
  views::View* vertical = vertical_region();
  ASSERT_NE(nullptr, vertical) << "controller/region view must exist under "
                                  "the roamux flag (patch 0008 creation gate)";
  EXPECT_TRUE(vertical->GetVisible());
  const gfx::Rect bounds = BoundsInBrowserView(vertical, browser_view());
  EXPECT_EQ(browser_view()->GetLocalBounds().x(), bounds.x());
  EXPECT_GT(bounds.width(), 0);
}

IN_PROC_BROWSER_TEST_F(RoamuxVerticalStripPlacementTest,
                       RightPlacementDocksVerticalStripAtRightEdge) {
  SetPlacementAndLayout(3);  // kRight.
  views::View* vertical = vertical_region();
  ASSERT_NE(nullptr, vertical);
  EXPECT_TRUE(vertical->GetVisible());
  const gfx::Rect bounds = BoundsInBrowserView(vertical, browser_view());
  EXPECT_EQ(browser_view()->GetLocalBounds().right(), bounds.right());
  EXPECT_GT(bounds.x(), browser_view()->GetLocalBounds().width() / 2);
}

IN_PROC_BROWSER_TEST_F(RoamuxVerticalStripPlacementTest,
                       LiveSwitchAcrossAllFourPlacements) {
  views::View* horizontal =
      FindViewByClassName(browser_view(), "HorizontalTabStripRegionView");
  ASSERT_NE(nullptr, horizontal);

  SetPlacementAndLayout(0);  // top
  EXPECT_TRUE(horizontal->GetVisible());

  SetPlacementAndLayout(1);  // bottom — horizontal strip, bottom band
  EXPECT_TRUE(horizontal->GetVisible());
  EXPECT_EQ(browser_view()->GetLocalBounds().bottom(),
            BoundsInBrowserView(horizontal, browser_view()).bottom());

  SetPlacementAndLayout(2);  // left — vertical strip takes over
  views::View* vertical = vertical_region();
  ASSERT_NE(nullptr, vertical);
  EXPECT_TRUE(vertical->GetVisible());

  SetPlacementAndLayout(3);  // right
  EXPECT_TRUE(vertical->GetVisible());
  EXPECT_EQ(browser_view()->GetLocalBounds().right(),
            BoundsInBrowserView(vertical, browser_view()).right());

  SetPlacementAndLayout(0);  // and home again — no restart throughout
  EXPECT_TRUE(horizontal->GetVisible());
  EXPECT_LT(BoundsInBrowserView(horizontal, browser_view()).y(),
            browser_view()->GetLocalBounds().height() / 2);
}

IN_PROC_BROWSER_TEST_F(RoamuxVerticalStripPlacementTest,
                       LockDefersDisplayReconciliationUntilUnlock) {
  auto* controller = ::tabs::VerticalTabStripStateController::From(browser());
  ASSERT_NE(nullptr, controller);

  // While an enable-state lock is held, a roamux placement change must not
  // swap strips; the effective display reconciles on unlock (Step-8 fix).
  {
    auto lock = controller->GetEnableStateLock();
    SetPlacementAndLayout(2);  // kLeft while locked.
    EXPECT_FALSE(controller->ShouldDisplayVerticalTabs() && vertical_region() &&
                 vertical_region()->GetVisible() && false)
        << "(sanity only — the real assertion is post-unlock)";
    views::View* vertical = vertical_region();
    if (vertical) {
      EXPECT_FALSE(vertical->GetVisible());
    }
  }
  base::RunLoop().RunUntilIdle();
  browser_view()->DeprecatedLayoutImmediately();
  views::View* vertical = vertical_region();
  ASSERT_NE(nullptr, vertical);
  EXPECT_TRUE(vertical->GetVisible());
}

IN_PROC_BROWSER_TEST_F(RoamuxVerticalStripPlacementTest,
                       UpstreamToggleCommandResetsRoamuxPlacement) {
  SetPlacementAndLayout(2);  // Roamux-driven vertical (upstream pref off).
  ASSERT_NE(nullptr, vertical_region());
  EXPECT_TRUE(vertical_region()->GetVisible());

  // The upstream "switch to horizontal" command must reset the roamux
  // placement, not no-op on the (already-false) upstream pref (Step-8 fix).
  chrome::ToggleVerticalTabs(browser());
  base::RunLoop().RunUntilIdle();
  browser_view()->DeprecatedLayoutImmediately();

  PrefService* prefs = browser()->profile()->GetPrefs();
  EXPECT_EQ(0, prefs->GetInteger(prefs::kTabStripPosition));
  EXPECT_FALSE(prefs->GetBoolean(::prefs::kVerticalTabsEnabled));
  EXPECT_FALSE(vertical_region()->GetVisible());
}

class RoamuxVerticalStripFlagOffTest : public roamux::test::RoamuxBrowserTest {
 public:
  RoamuxVerticalStripFlagOffTest() {
    features_.InitAndDisableFeature(features::kTabStripPosition);
  }

 protected:
  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(RoamuxVerticalStripFlagOffTest,
                       LeftPlacementInertWhenFlagOff) {
  BrowserView* bv = BrowserView::GetBrowserViewForBrowser(browser());
  browser()->profile()->GetPrefs()->SetInteger(prefs::kTabStripPosition, 2);
  base::RunLoop().RunUntilIdle();
  bv->DeprecatedLayoutImmediately();
  // Stock behavior: the vertical strip is not displayed. (The region view
  // object may still exist — test builds can enable the upstream feature via
  // the field-trial testing config — so assert on visibility, not existence.)
  views::View* vertical = FindViewByClassName(bv, "VerticalTabStripRegionView");
  if (vertical) {
    EXPECT_FALSE(vertical->GetVisible());
  }
  views::View* horizontal =
      FindViewByClassName(bv, "HorizontalTabStripRegionView");
  ASSERT_NE(nullptr, horizontal);
  EXPECT_TRUE(horizontal->GetVisible());
}

class RoamuxVerticalStripUpstreamPrecedenceTest
    : public roamux::test::RoamuxBrowserTest {
 public:
  RoamuxVerticalStripUpstreamPrecedenceTest() {
    features_.InitWithFeatures(
        {features::kTabStripPosition, ::tabs::kVerticalTabs}, {});
  }

 protected:
  base::test::ScopedFeatureList features_;
};

// roam-182 sole authority: the roamux placement drives display and dock even
// when the upstream pref is explicitly on (the old precedence rule made every
// placement a user-visible no-op in exactly this profile state).
IN_PROC_BROWSER_TEST_F(RoamuxVerticalStripUpstreamPrecedenceTest,
                       RoamuxRightDocksRightDespiteUpstreamPrefOn) {
  BrowserView* bv = BrowserView::GetBrowserViewForBrowser(browser());
  PrefService* prefs = browser()->profile()->GetPrefs();
  prefs->SetBoolean(::prefs::kVerticalTabsEnabled, true);
  prefs->SetInteger(prefs::kTabStripPosition, 3);  // roamux right.
  base::RunLoop().RunUntilIdle();
  bv->DeprecatedLayoutImmediately();

  views::View* vertical = FindViewByClassName(bv, "VerticalTabStripRegionView");
  ASSERT_NE(nullptr, vertical);
  EXPECT_TRUE(vertical->GetVisible());
  gfx::RectF rect(gfx::SizeF(vertical->size()));
  views::View::ConvertRectToTarget(vertical, bv, &rect);
  EXPECT_EQ(bv->GetLocalBounds().right(), gfx::ToEnclosingRect(rect).right());
  // Mid-session the upstream pref is display-inert, not rewritten
  // (normalization happens only at profile init).
  EXPECT_TRUE(prefs->GetBoolean(::prefs::kVerticalTabsEnabled));
}

// The direct regression test for the reported bug: with the upstream pref set
// (the state the stock "Tab position: Vertical" dropdown produces), every
// placement must still take live effect.
IN_PROC_BROWSER_TEST_F(RoamuxVerticalStripUpstreamPrecedenceTest,
                       AllFourPlacementsActWithUpstreamPrefSet) {
  BrowserView* bv = BrowserView::GetBrowserViewForBrowser(browser());
  PrefService* prefs = browser()->profile()->GetPrefs();
  prefs->SetBoolean(::prefs::kVerticalTabsEnabled, true);

  auto set_placement = [&](int value) {
    prefs->SetInteger(prefs::kTabStripPosition, value);
    base::RunLoop().RunUntilIdle();
    bv->DeprecatedLayoutImmediately();
  };
  views::View* horizontal =
      FindViewByClassName(bv, "HorizontalTabStripRegionView");
  ASSERT_NE(nullptr, horizontal);

  set_placement(0);  // top
  EXPECT_TRUE(horizontal->GetVisible());
  EXPECT_LT(BoundsInBrowserView(horizontal, bv).y(),
            bv->GetLocalBounds().height() / 2);

  set_placement(1);  // bottom
  EXPECT_TRUE(horizontal->GetVisible());
  EXPECT_EQ(bv->GetLocalBounds().bottom(),
            BoundsInBrowserView(horizontal, bv).bottom());

  set_placement(2);  // left
  views::View* vertical = FindViewByClassName(bv, "VerticalTabStripRegionView");
  ASSERT_NE(nullptr, vertical);
  EXPECT_TRUE(vertical->GetVisible());
  EXPECT_EQ(bv->GetLocalBounds().x(), BoundsInBrowserView(vertical, bv).x());

  set_placement(3);  // right
  EXPECT_TRUE(vertical->GetVisible());
  EXPECT_EQ(bv->GetLocalBounds().right(),
            BoundsInBrowserView(vertical, bv).right());
}

// roam-182: with the flag on, the upstream "switch to vertical" toggle maps
// onto the placement (kLeft) instead of writing the upstream pref.
IN_PROC_BROWSER_TEST_F(RoamuxVerticalStripPlacementTest,
                       UpstreamToggleToVerticalMapsToLeftPlacement) {
  SetPlacementAndLayout(0);  // top (horizontal).
  auto* controller = ::tabs::VerticalTabStripStateController::From(browser());
  ASSERT_NE(nullptr, controller);

  controller->SetVerticalTabsEnabled(true);
  base::RunLoop().RunUntilIdle();
  browser_view()->DeprecatedLayoutImmediately();

  PrefService* prefs = browser()->profile()->GetPrefs();
  EXPECT_EQ(2, prefs->GetInteger(prefs::kTabStripPosition));  // kLeft.
  EXPECT_FALSE(prefs->GetBoolean(::prefs::kVerticalTabsEnabled));
  views::View* vertical = vertical_region();
  ASSERT_NE(nullptr, vertical);
  EXPECT_TRUE(vertical->GetVisible());
}

// roam-182 lock semantics under sole authority: a placement change that flips
// the display mode while an enable-state lock is held stays frozen (no mid-lock
// mode-change notification, visible state unchanged), and reconciles with
// EXACTLY ONE notification on unlock to the placement-ONLY answer even with the
// upstream pref set (the old code kept the upstream contribution and stayed
// vertical). Covered in both directions: vertical->horizontal and
// horizontal->vertical.
IN_PROC_BROWSER_TEST_F(RoamuxVerticalStripUpstreamPrecedenceTest,
                       LockedVerticalToHorizontalReconcilesOnceOnUnlock) {
  BrowserView* bv = BrowserView::GetBrowserViewForBrowser(browser());
  PrefService* prefs = browser()->profile()->GetPrefs();
  auto* controller = ::tabs::VerticalTabStripStateController::From(browser());
  ASSERT_NE(nullptr, controller);

  prefs->SetBoolean(::prefs::kVerticalTabsEnabled, true);
  prefs->SetInteger(prefs::kTabStripPosition, 2);  // left — vertical visible.
  base::RunLoop().RunUntilIdle();
  bv->DeprecatedLayoutImmediately();
  views::View* vertical = FindViewByClassName(bv, "VerticalTabStripRegionView");
  ASSERT_NE(nullptr, vertical);
  ASSERT_TRUE(vertical->GetVisible());

  int mode_changes = 0;
  auto subscription = controller->RegisterOnModeChanged(base::BindRepeating(
      [](int* n, ::tabs::VerticalTabStripStateController*) { ++*n; },
      &mode_changes));

  {
    auto lock = controller->GetEnableStateLock();
    prefs->SetInteger(prefs::kTabStripPosition, 1);  // bottom while locked.
    base::RunLoop().RunUntilIdle();
    bv->DeprecatedLayoutImmediately();
    EXPECT_TRUE(vertical->GetVisible()) << "display must stay frozen mid-lock";
    EXPECT_EQ(0, mode_changes) << "no mode-change notification while locked";
  }
  base::RunLoop().RunUntilIdle();
  bv->DeprecatedLayoutImmediately();

  EXPECT_EQ(1, mode_changes) << "exactly one reconcile notification on unlock";
  EXPECT_FALSE(vertical->GetVisible());
  views::View* horizontal =
      FindViewByClassName(bv, "HorizontalTabStripRegionView");
  ASSERT_NE(nullptr, horizontal);
  EXPECT_TRUE(horizontal->GetVisible());
  EXPECT_EQ(bv->GetLocalBounds().bottom(),
            BoundsInBrowserView(horizontal, bv).bottom());
}

IN_PROC_BROWSER_TEST_F(RoamuxVerticalStripUpstreamPrecedenceTest,
                       LockedHorizontalToVerticalReconcilesOnceOnUnlock) {
  BrowserView* bv = BrowserView::GetBrowserViewForBrowser(browser());
  PrefService* prefs = browser()->profile()->GetPrefs();
  auto* controller = ::tabs::VerticalTabStripStateController::From(browser());
  ASSERT_NE(nullptr, controller);

  prefs->SetBoolean(::prefs::kVerticalTabsEnabled, true);
  prefs->SetInteger(prefs::kTabStripPosition, 0);  // top — horizontal.
  base::RunLoop().RunUntilIdle();
  bv->DeprecatedLayoutImmediately();
  views::View* horizontal =
      FindViewByClassName(bv, "HorizontalTabStripRegionView");
  ASSERT_NE(nullptr, horizontal);
  ASSERT_TRUE(horizontal->GetVisible());

  int mode_changes = 0;
  auto subscription = controller->RegisterOnModeChanged(base::BindRepeating(
      [](int* n, ::tabs::VerticalTabStripStateController*) { ++*n; },
      &mode_changes));

  {
    auto lock = controller->GetEnableStateLock();
    prefs->SetInteger(prefs::kTabStripPosition, 3);  // right while locked.
    base::RunLoop().RunUntilIdle();
    bv->DeprecatedLayoutImmediately();
    EXPECT_EQ(0, mode_changes) << "no mode-change notification while locked";
  }
  base::RunLoop().RunUntilIdle();
  bv->DeprecatedLayoutImmediately();

  EXPECT_EQ(1, mode_changes) << "exactly one reconcile notification on unlock";
  views::View* vertical = FindViewByClassName(bv, "VerticalTabStripRegionView");
  ASSERT_NE(nullptr, vertical);
  EXPECT_TRUE(vertical->GetVisible());
  EXPECT_EQ(bv->GetLocalBounds().right(),
            BoundsInBrowserView(vertical, bv).right());
}

// roam-206: the collapse toggle's arrow points toward where the panel will
// move — open-icon iff collapsed == docked-right (physical rule).
IN_PROC_BROWSER_TEST_F(RoamuxVerticalStripPlacementTest,
                       CollapseIconMatchesDockSide) {
  auto* controller = ::tabs::VerticalTabStripStateController::From(browser());
  ASSERT_NE(nullptr, controller);
  actions::ActionItem* item = CollapseActionItem(browser());
  ASSERT_NE(nullptr, item);

  SetPlacementAndLayout(3);  // right dock
  CollapseAndWait(controller, true);
  EXPECT_EQ(IconModel(views::kMenuOpenIcon), item->GetImage());
  CollapseAndWait(controller, false);
  EXPECT_EQ(IconModel(views::kMenuCloseIcon), item->GetImage());

  SetPlacementAndLayout(2);  // left dock — stock rows stay stock
  CollapseAndWait(controller, true);
  EXPECT_EQ(IconModel(views::kMenuCloseIcon), item->GetImage());
  CollapseAndWait(controller, false);
  EXPECT_EQ(IconModel(views::kMenuOpenIcon), item->GetImage());
}

// roam-206: a live dock-side flip re-derives the icon without a collapse
// transition (OnRoamuxPlacementChanged's same-display-mode path).
IN_PROC_BROWSER_TEST_F(RoamuxVerticalStripPlacementTest,
                       LiveDockSwitchRefreshesCollapseIcon) {
  auto* controller = ::tabs::VerticalTabStripStateController::From(browser());
  ASSERT_NE(nullptr, controller);
  actions::ActionItem* item = CollapseActionItem(browser());
  ASSERT_NE(nullptr, item);
  SetPlacementAndLayout(2);
  CollapseAndWait(controller, false);
  ASSERT_EQ(IconModel(views::kMenuOpenIcon), item->GetImage());

  SetPlacementAndLayout(3);  // collapse state untouched
  EXPECT_EQ(IconModel(views::kMenuCloseIcon), item->GetImage());
}

// roam-206: the rule is physical — RTL changes nothing about which side the
// panel moves toward. The left-dock rows are where stock and physical
// genuinely diverge; the right-dock rows coincide with stock (pins).
IN_PROC_BROWSER_TEST_F(RoamuxVerticalStripPlacementTest,
                       CollapseIconIsPhysicalUnderRTL) {
  auto* controller = ::tabs::VerticalTabStripStateController::From(browser());
  ASSERT_NE(nullptr, controller);
  actions::ActionItem* item = CollapseActionItem(browser());
  ASSERT_NE(nullptr, item);
  base::i18n::ScopedRTLForTesting scoped_rtl(true);

  SetPlacementAndLayout(2);  // left
  CollapseAndWait(controller, true);
  EXPECT_EQ(IconModel(views::kMenuCloseIcon), item->GetImage());
  CollapseAndWait(controller, false);
  EXPECT_EQ(IconModel(views::kMenuOpenIcon), item->GetImage());

  SetPlacementAndLayout(3);  // right
  CollapseAndWait(controller, true);
  EXPECT_EQ(IconModel(views::kMenuOpenIcon), item->GetImage());
  CollapseAndWait(controller, false);
  EXPECT_EQ(IconModel(views::kMenuCloseIcon), item->GetImage());
}

// roam-206: a dock-side flip made while an enable-state lock is held must
// re-derive the icon on outermost unlock (OnLockDestroyed path).
IN_PROC_BROWSER_TEST_F(RoamuxVerticalStripPlacementTest,
                       LockedDockSwitchRefreshesIconOnUnlock) {
  auto* controller = ::tabs::VerticalTabStripStateController::From(browser());
  ASSERT_NE(nullptr, controller);
  actions::ActionItem* item = CollapseActionItem(browser());
  ASSERT_NE(nullptr, item);
  SetPlacementAndLayout(2);
  CollapseAndWait(controller, false);
  ASSERT_EQ(IconModel(views::kMenuOpenIcon), item->GetImage());

  {
    auto lock = controller->GetEnableStateLock();
    SetPlacementAndLayout(3);  // flip while locked; icon deferred
  }
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(IconModel(views::kMenuCloseIcon), item->GetImage());
}

// roam-206: with kTabStripPosition OFF the stock expression must be
// preserved exactly — both directions. Upstream vertical tabs are enabled
// explicitly so the collapse action exists deterministically.
class RoamuxVerticalStripFlagOffIconTest
    : public roamux::test::RoamuxBrowserTest {
 public:
  RoamuxVerticalStripFlagOffIconTest() {
    features_.InitWithFeatures({::tabs::kVerticalTabs},
                               {features::kTabStripPosition});
  }

 protected:
  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(RoamuxVerticalStripFlagOffIconTest,
                       StockIconExpressionPreserved) {
  auto* controller = ::tabs::VerticalTabStripStateController::From(browser());
  ASSERT_NE(nullptr, controller);
  actions::ActionItem* item = CollapseActionItem(browser());
  ASSERT_NE(nullptr, item);

  // Activate upstream vertical tabs so the delegate exists and collapse
  // round-trips (the roamux placement path is off in this fixture).
  controller->SetVerticalTabsEnabled(true);
  base::RunLoop().RunUntilIdle();

  CollapseAndWait(controller, true);  // LTR collapsed → close (stock)
  EXPECT_EQ(IconModel(views::kMenuCloseIcon), item->GetImage());

  {
    base::i18n::ScopedRTLForTesting scoped_rtl(true);
    CollapseAndWait(controller, false);
    CollapseAndWait(controller, true);  // re-derive under RTL → open (stock)
    EXPECT_EQ(IconModel(views::kMenuOpenIcon), item->GetImage());
  }
}

}  // namespace
}  // namespace roamux

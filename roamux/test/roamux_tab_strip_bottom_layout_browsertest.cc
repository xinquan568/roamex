// SPDX-License-Identifier: Apache-2.0
// roam-7 (I-1.2) layout acceptance: bottom placement docks ONLY the strip at
// the browser view's bottom edge (toolbar/bookmarks stay top), switches live
// on a pref flip without restart, is flag-gated, and stays inert while
// upstream vertical tabs are displayed (coexistence rule D1).
// (TDD: written RED before patch 0007.)

#include <string_view>

#include "base/run_loop.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/features.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "roamux/common/roamux_features.h"
#include "roamux/common/roamux_prefs.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

namespace roamux {
namespace {

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

class RoamuxTabStripBottomLayoutTest : public InProcessBrowserTest {
 public:
  RoamuxTabStripBottomLayoutTest() {
    features_.InitAndEnableFeature(features::kTabStripPosition);
  }

 protected:
  BrowserView* browser_view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }

  views::View* horizontal_strip_region() {
    return FindViewByClassName(browser_view(), "HorizontalTabStripRegionView");
  }

  // The strip region's bounds in BrowserView coordinates.
  gfx::Rect StripBoundsInBrowserView() {
    views::View* strip = horizontal_strip_region();
    EXPECT_NE(nullptr, strip);
    gfx::RectF rect(gfx::SizeF(strip->size()));
    views::View::ConvertRectToTarget(strip, browser_view(), &rect);
    return gfx::ToEnclosingRect(rect);
  }

  void SetPlacementAndLayout(int value) {
    browser()->profile()->GetPrefs()->SetInteger(prefs::kTabStripPosition,
                                                 value);
    base::RunLoop().RunUntilIdle();
    views::View* bv = browser_view();
    bv->DeprecatedLayoutImmediately();
  }

  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(RoamuxTabStripBottomLayoutTest,
                       BottomPlacementDocksStripAtBottomEdge) {
  SetPlacementAndLayout(1);  // kBottom.
  const gfx::Rect strip = StripBoundsInBrowserView();
  const gfx::Rect bv_bounds = browser_view()->GetLocalBounds();
  // Bottom-aligned, full width, non-empty height.
  EXPECT_EQ(bv_bounds.bottom(), strip.bottom());
  EXPECT_GT(strip.height(), 0);
  EXPECT_GT(strip.y(), bv_bounds.height() / 2)
      << "strip should sit in the lower half";
  // The toolbar stays on top: contents/top-container occupy the area above.
  EXPECT_GT(strip.y(), 0);
}

IN_PROC_BROWSER_TEST_F(RoamuxTabStripBottomLayoutTest,
                       LiveSwitchRelocatesWithoutRestart) {
  SetPlacementAndLayout(0);  // kTop.
  const gfx::Rect top_bounds = StripBoundsInBrowserView();
  EXPECT_LT(top_bounds.y(), browser_view()->GetLocalBounds().height() / 2);

  SetPlacementAndLayout(1);  // kBottom — same window, no restart.
  const gfx::Rect bottom_bounds = StripBoundsInBrowserView();
  EXPECT_EQ(browser_view()->GetLocalBounds().bottom(), bottom_bounds.bottom());

  SetPlacementAndLayout(0);  // And back.
  const gfx::Rect back_bounds = StripBoundsInBrowserView();
  EXPECT_LT(back_bounds.y(), browser_view()->GetLocalBounds().height() / 2);
}

class RoamuxTabStripBottomLayoutFlagOffTest : public InProcessBrowserTest {
 public:
  RoamuxTabStripBottomLayoutFlagOffTest() {
    features_.InitAndDisableFeature(features::kTabStripPosition);
  }

 protected:
  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(RoamuxTabStripBottomLayoutFlagOffTest,
                       PrefIsInertWhenFlagOff) {
  BrowserView* bv = BrowserView::GetBrowserViewForBrowser(browser());
  browser()->profile()->GetPrefs()->SetInteger(prefs::kTabStripPosition, 1);
  base::RunLoop().RunUntilIdle();
  bv->DeprecatedLayoutImmediately();
  views::View* strip = FindViewByClassName(bv, "HorizontalTabStripRegionView");
  ASSERT_NE(nullptr, strip);
  gfx::RectF rect(gfx::SizeF(strip->size()));
  views::View::ConvertRectToTarget(strip, bv, &rect);
  // Stock layout: the strip stays in the top half.
  EXPECT_LT(gfx::ToEnclosingRect(rect).y(), bv->GetLocalBounds().height() / 2);
}

// Coexistence rule D1: while upstream vertical tabs are displayed, the roamux
// placement is structurally inert — the vertical strip owns the layout and the
// horizontal strip stays hidden regardless of roamux.tabs.strip_position.
class RoamuxTabStripCoexistenceTest : public InProcessBrowserTest {
 public:
  RoamuxTabStripCoexistenceTest() {
    features_.InitWithFeatures(
        {features::kTabStripPosition, ::tabs::kVerticalTabs}, {});
  }

 protected:
  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(RoamuxTabStripCoexistenceTest,
                       UpstreamVerticalTabsWinOverRoamuxBottom) {
  BrowserView* bv = BrowserView::GetBrowserViewForBrowser(browser());
  PrefService* prefs = browser()->profile()->GetPrefs();
  prefs->SetBoolean(::prefs::kVerticalTabsEnabled, true);
  prefs->SetInteger(prefs::kTabStripPosition, 1);  // roamux bottom.
  base::RunLoop().RunUntilIdle();
  bv->DeprecatedLayoutImmediately();

  views::View* vertical = FindViewByClassName(bv, "VerticalTabStripRegionView");
  ASSERT_NE(nullptr, vertical);
  EXPECT_TRUE(vertical->GetVisible());

  // The horizontal strip does not occupy a bottom band: either hidden or not
  // bottom-aligned.
  views::View* horizontal =
      FindViewByClassName(bv, "HorizontalTabStripRegionView");
  ASSERT_NE(nullptr, horizontal);
  if (horizontal->GetVisible()) {
    gfx::RectF rect(gfx::SizeF(horizontal->size()));
    views::View::ConvertRectToTarget(horizontal, bv, &rect);
    EXPECT_NE(bv->GetLocalBounds().bottom(),
              gfx::ToEnclosingRect(rect).bottom());
  }
}

}  // namespace
}  // namespace roamux

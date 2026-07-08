// SPDX-License-Identifier: Apache-2.0
// roam-8 (I-1.3): placement left/right reuses the upstream vertical tab strip
// (maintainer-authorized surface): display mapping (both docks), right-dock
// geometry, live switching across all four placements, flag-off inertness, and
// upstream-pref precedence. (TDD: written RED before patch 0008.)

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
#include "roamex/common/roamex_features.h"
#include "roamex/common/roamex_prefs.h"
#include "ui/views/view.h"

namespace roamex {
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

gfx::Rect BoundsInBrowserView(views::View* view, BrowserView* bv) {
  gfx::RectF rect(gfx::SizeF(view->size()));
  views::View::ConvertRectToTarget(view, bv, &rect);
  return gfx::ToEnclosingRect(rect);
}

class RoamexVerticalStripPlacementTest : public InProcessBrowserTest {
 public:
  RoamexVerticalStripPlacementTest() {
    // Deliberately does NOT enable the upstream vertical-tabs feature flags:
    // the roamex path must work without them (plan D1).
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

IN_PROC_BROWSER_TEST_F(RoamexVerticalStripPlacementTest,
                       LeftPlacementDisplaysVerticalStripAtLeadingEdge) {
  SetPlacementAndLayout(2);  // kLeft.
  views::View* vertical = vertical_region();
  ASSERT_NE(nullptr, vertical) << "controller/region view must exist under "
                                  "the roamex flag (patch 0008 creation gate)";
  EXPECT_TRUE(vertical->GetVisible());
  const gfx::Rect bounds = BoundsInBrowserView(vertical, browser_view());
  EXPECT_EQ(browser_view()->GetLocalBounds().x(), bounds.x());
  EXPECT_GT(bounds.width(), 0);
}

IN_PROC_BROWSER_TEST_F(RoamexVerticalStripPlacementTest,
                       RightPlacementDocksVerticalStripAtRightEdge) {
  SetPlacementAndLayout(3);  // kRight.
  views::View* vertical = vertical_region();
  ASSERT_NE(nullptr, vertical);
  EXPECT_TRUE(vertical->GetVisible());
  const gfx::Rect bounds = BoundsInBrowserView(vertical, browser_view());
  EXPECT_EQ(browser_view()->GetLocalBounds().right(), bounds.right());
  EXPECT_GT(bounds.x(), browser_view()->GetLocalBounds().width() / 2);
}

IN_PROC_BROWSER_TEST_F(RoamexVerticalStripPlacementTest,
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

class RoamexVerticalStripFlagOffTest : public InProcessBrowserTest {
 public:
  RoamexVerticalStripFlagOffTest() {
    features_.InitAndDisableFeature(features::kTabStripPosition);
  }

 protected:
  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(RoamexVerticalStripFlagOffTest,
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

class RoamexVerticalStripUpstreamPrecedenceTest : public InProcessBrowserTest {
 public:
  RoamexVerticalStripUpstreamPrecedenceTest() {
    features_.InitWithFeatures(
        {features::kTabStripPosition, ::tabs::kVerticalTabs}, {});
  }

 protected:
  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(RoamexVerticalStripUpstreamPrecedenceTest,
                       UpstreamPrefOnKeepsLeadingDockDespiteRoamexRight) {
  BrowserView* bv = BrowserView::GetBrowserViewForBrowser(browser());
  PrefService* prefs = browser()->profile()->GetPrefs();
  prefs->SetBoolean(::prefs::kVerticalTabsEnabled, true);
  prefs->SetInteger(prefs::kTabStripPosition, 3);  // roamex right.
  base::RunLoop().RunUntilIdle();
  bv->DeprecatedLayoutImmediately();

  views::View* vertical = FindViewByClassName(bv, "VerticalTabStripRegionView");
  ASSERT_NE(nullptr, vertical);
  EXPECT_TRUE(vertical->GetVisible());
  gfx::RectF rect(gfx::SizeF(vertical->size()));
  views::View::ConvertRectToTarget(vertical, bv, &rect);
  // Upstream pref wins: leading (left) dock.
  EXPECT_EQ(bv->GetLocalBounds().x(), gfx::ToEnclosingRect(rect).x());
  // And roamex never wrote the upstream pref's value back off/on.
  EXPECT_TRUE(prefs->GetBoolean(::prefs::kVerticalTabsEnabled));
}

}  // namespace
}  // namespace roamex

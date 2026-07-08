// SPDX-License-Identifier: Apache-2.0
// roam-9 (I-1.4): the E1 frame/RTL/fullscreen verification matrix — macOS
// caption (traffic-light) exclusion is preserved with a bottom strip, Roamex
// placements are PHYSICAL (RTL-invariant, plan D1), fullscreen enter/exit
// stays sane under every placement, and app (PWA) windows ignore the setting.

#include <string_view>

#include "base/command_line.h"
#include "base/i18n/base_i18n_switches.h"
#include "base/run_loop.h"
#include "base/test/scoped_feature_list.h"
#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
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

gfx::Rect BoundsIn(views::View* view, views::View* ancestor) {
  gfx::RectF rect(gfx::SizeF(view->size()));
  views::View::ConvertRectToTarget(view, ancestor, &rect);
  return gfx::ToEnclosingRect(rect);
}

class RoamexFrameMatrixTest : public InProcessBrowserTest {
 public:
  RoamexFrameMatrixTest() {
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

  base::test::ScopedFeatureList features_;
};

#if BUILDFLAG(IS_MAC)
// R1: with the strip at the bottom, the TOOLBAR becomes the topmost view and
// must reserve the macOS caption (traffic-light) exclusion — it goes through
// GetBoundsWithExclusion in CalculateTopContainerLayoutImpl, so its origin
// must be pushed off the window's top-leading corner.
IN_PROC_BROWSER_TEST_F(RoamexFrameMatrixTest,
                       BottomStripPreservesCaptionExclusion) {
  SetPlacementAndLayout(1);  // kBottom.
  views::View* toolbar = FindViewByClassName(browser_view(), "ToolbarView");
  ASSERT_NE(nullptr, toolbar);
  ASSERT_TRUE(toolbar->GetVisible());
  const gfx::Rect toolbar_bounds = BoundsIn(toolbar, browser_view());
  // The caption exclusion must displace the toolbar from the raw top-leading
  // corner (traffic lights overlay the client area when the titlebar is
  // hidden). Either the origin shifts or the row starts below the buttons.
  EXPECT_TRUE(toolbar_bounds.x() > 0 || toolbar_bounds.y() > 0)
      << "toolbar at " << toolbar_bounds.ToString()
      << " ignores the caption exclusion";
  // And the bottom strip band never reaches the top-leading region.
  views::View* strip =
      FindViewByClassName(browser_view(), "HorizontalTabStripRegionView");
  ASSERT_NE(nullptr, strip);
  EXPECT_GT(BoundsIn(strip, browser_view()).y(),
            browser_view()->GetLocalBounds().height() / 2);
}
#endif  // BUILDFLAG(IS_MAC)

// D1: placements are PHYSICAL — identical geometry under RTL.
class RoamexRtlPlacementTest : public RoamexFrameMatrixTest {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    RoamexFrameMatrixTest::SetUpCommandLine(command_line);
    command_line->AppendSwitchASCII(::switches::kForceUIDirection,
                                    ::switches::kForceDirectionRTL);
  }
};

IN_PROC_BROWSER_TEST_F(RoamexRtlPlacementTest,
                       RightPlacementStaysPhysicalRightInRtl) {
  SetPlacementAndLayout(3);  // kRight.
  views::View* vertical =
      FindViewByClassName(browser_view(), "VerticalTabStripRegionView");
  ASSERT_NE(nullptr, vertical);
  EXPECT_TRUE(vertical->GetVisible());
  EXPECT_EQ(browser_view()->GetLocalBounds().right(),
            BoundsIn(vertical, browser_view()).right());
}

IN_PROC_BROWSER_TEST_F(RoamexRtlPlacementTest,
                       LeftPlacementStaysPhysicalLeftInRtl) {
  SetPlacementAndLayout(2);  // kLeft.
  views::View* vertical =
      FindViewByClassName(browser_view(), "VerticalTabStripRegionView");
  ASSERT_NE(nullptr, vertical);
  EXPECT_TRUE(vertical->GetVisible());
  EXPECT_EQ(browser_view()->GetLocalBounds().x(),
            BoundsIn(vertical, browser_view()).x());
}

IN_PROC_BROWSER_TEST_F(RoamexRtlPlacementTest, BottomPlacementFullWidthInRtl) {
  SetPlacementAndLayout(1);  // kBottom.
  views::View* strip =
      FindViewByClassName(browser_view(), "HorizontalTabStripRegionView");
  ASSERT_NE(nullptr, strip);
  const gfx::Rect bounds = BoundsIn(strip, browser_view());
  EXPECT_EQ(browser_view()->GetLocalBounds().bottom(), bounds.bottom());
  EXPECT_EQ(browser_view()->GetLocalBounds().width(), bounds.width());
}

// D3: fullscreen enter/exit stays sane under every placement.
IN_PROC_BROWSER_TEST_F(RoamexFrameMatrixTest, FullscreenMatrixIsSane) {
  for (int placement = 0; placement <= 3; ++placement) {
    SCOPED_TRACE(placement);
    SetPlacementAndLayout(placement);

    ui_test_utils::ToggleFullscreenModeAndWait(browser());
    base::RunLoop().RunUntilIdle();
    browser_view()->DeprecatedLayoutImmediately();
    // Sanity in fullscreen: the contents container exists and stays inside
    // the browser view.
    views::View* contents =
        FindViewByClassName(browser_view(), "ContentsWebView");
    ASSERT_NE(nullptr, contents);
    const gfx::Rect contents_bounds = BoundsIn(contents, browser_view());
    EXPECT_FALSE(contents_bounds.IsEmpty());
    EXPECT_TRUE(browser_view()->GetLocalBounds().Contains(contents_bounds));

    ui_test_utils::ToggleFullscreenModeAndWait(browser());
    base::RunLoop().RunUntilIdle();
    browser_view()->DeprecatedLayoutImmediately();
    EXPECT_FALSE(browser_view()->IsFullscreen());
  }
  // Back at top: stock-shaped layout.
  SetPlacementAndLayout(0);
  views::View* strip =
      FindViewByClassName(browser_view(), "HorizontalTabStripRegionView");
  ASSERT_NE(nullptr, strip);
  EXPECT_LT(BoundsIn(strip, browser_view()).y(),
            browser_view()->GetLocalBounds().height() / 2);
}

// D4: an app (PWA-style) window ignores the placement entirely.
IN_PROC_BROWSER_TEST_F(RoamexFrameMatrixTest, AppWindowIgnoresPlacement) {
  browser()->profile()->GetPrefs()->SetInteger(prefs::kTabStripPosition, 1);
  Browser* app_browser = Browser::Create(Browser::CreateParams::CreateForApp(
      "roamex_test_app", /*trusted_source=*/true, gfx::Rect(),
      browser()->profile(), /*user_gesture=*/true));
  ASSERT_NE(nullptr, app_browser);
  app_browser->window()->Show();
  base::RunLoop().RunUntilIdle();

  BrowserView* app_view = BrowserView::GetBrowserViewForBrowser(app_browser);
  ASSERT_NE(nullptr, app_view);
  app_view->DeprecatedLayoutImmediately();
  // App windows have no tab strip; the roamex placement must not conjure one
  // (bottom band) nor a vertical strip.
  views::View* strip =
      FindViewByClassName(app_view, "HorizontalTabStripRegionView");
  if (strip && strip->GetVisible()) {
    EXPECT_LT(BoundsIn(strip, app_view).y(),
              app_view->GetLocalBounds().height() / 2)
        << "app window grew a bottom strip band";
  }
  views::View* vertical =
      FindViewByClassName(app_view, "VerticalTabStripRegionView");
  if (vertical) {
    EXPECT_FALSE(vertical->GetVisible());
  }
}

}  // namespace
}  // namespace roamex

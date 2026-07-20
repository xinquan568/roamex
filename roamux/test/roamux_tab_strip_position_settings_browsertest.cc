// SPDX-License-Identifier: Apache-2.0
// roam-6 (I-1.1) WB-T4: the settings DOM surface — the Appearance "Tab strip
// position (Roamux)" row is flag-gated and live-bound (both directions) to
// roamux.tabs.strip_position (TDD/P6: written RED before patch 0006).

#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/features.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "roamux/common/roamux_features.h"
#include "roamux/common/roamux_prefs.h"
#include "roamux/test/support/roamux_browser_test.h"

namespace roamux {
namespace {

// Recursively searches open shadow roots for an element id, polling until it
// appears (Polymer stamps dom-if templates asynchronously). Resolves to true
// iff found within the deadline.
constexpr char kWaitForIdScript[] = R"(
    (async () => {
      const deepQuery = (root, id) => {
        const direct = root.querySelector('#' + id);
        if (direct) return direct;
        for (const el of root.querySelectorAll('*')) {
          if (el.shadowRoot) {
            const hit = deepQuery(el.shadowRoot, id);
            if (hit) return hit;
          }
        }
        return null;
      };
      for (let i = 0; i < %d; i++) {
        if (deepQuery(document, '%s')) return true;
        await new Promise(r => setTimeout(r, 100));
      }
      return false;
    })();
)";

std::string WaitForIdScript(const std::string& id, int attempts) {
  return base::StringPrintf(kWaitForIdScript, attempts, id.c_str());
}

// Runs `body` with `el` bound to the deep-queried #roamuxTabStripPosition
// element (assumes it exists).
std::string WithRowScript(const std::string& body) {
  return base::StringPrintf(R"(
      (async () => {
        const deepQuery = (root, id) => {
          const direct = root.querySelector('#' + id);
          if (direct) return direct;
          for (const el of root.querySelectorAll('*')) {
            if (el.shadowRoot) {
              const hit = deepQuery(el.shadowRoot, id);
              if (hit) return hit;
            }
          }
          return null;
        };
        const el = deepQuery(document, 'roamuxTabStripPosition');
        if (!el) return 'missing';
        %s
      })();
  )",
                            body.c_str());
}

class RoamuxTabStripPositionSettingsTest
    : public roamux::test::RoamuxBrowserTest {
 public:
  RoamuxTabStripPositionSettingsTest() {
    features_.InitAndEnableFeature(features::kTabStripPosition);
  }

 protected:
  content::WebContents* NavigateToAppearance() {
    EXPECT_TRUE(ui_test_utils::NavigateToURL(
        browser(), GURL("chrome://settings/appearance")));
    content::WebContents* web_contents =
        browser()->tab_strip_model()->GetActiveWebContents();
    // The page is interactive once a stock Appearance anchor is stamped.
    EXPECT_EQ(true,
              content::EvalJs(web_contents, WaitForIdScript("themeRow", 100)));
    return web_contents;
  }

  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(RoamuxTabStripPositionSettingsTest,
                       RowPresentAndLiveBoundWhenFlagOn) {
  content::WebContents* web_contents = NavigateToAppearance();

  // The roamux row is stamped.
  EXPECT_EQ(true,
            content::EvalJs(web_contents,
                            WaitForIdScript("roamuxTabStripPosition", 100)));

  // JS -> pref: pick "Bottom" (value 1) in the dropdown.
  EXPECT_EQ("ok", content::EvalJs(web_contents, WithRowScript(R"(
        const select = el.shadowRoot.querySelector('select');
        if (!select) return 'no-select';
        select.value = '1';
        select.dispatchEvent(new Event('change'));
        return 'ok';
  )")));
  PrefService* prefs = browser()->profile()->GetPrefs();
  // settings_private writes are async over the extension API; poll (bounded).
  for (int i = 0; i < 1000 && prefs->GetInteger(prefs::kTabStripPosition) != 1;
       ++i) {
    base::RunLoop().RunUntilIdle();
  }
  EXPECT_EQ(1, prefs->GetInteger(prefs::kTabStripPosition));

  // Pref -> JS: a C++ pref write must reflect back into the dropdown live.
  prefs->SetInteger(prefs::kTabStripPosition, 3);
  EXPECT_EQ("3", content::EvalJs(web_contents, WithRowScript(R"(
        const select = el.shadowRoot.querySelector('select');
        if (!select) return 'no-select';
        for (let i = 0; i < 100; i++) {
          if (select.value === '3') return select.value;
          await new Promise(r => setTimeout(r, 100));
        }
        return select.value;
  )")));
}

class RoamuxTabStripPositionSettingsFlagOffTest
    : public roamux::test::RoamuxBrowserTest {
 public:
  RoamuxTabStripPositionSettingsFlagOffTest() {
    features_.InitAndDisableFeature(features::kTabStripPosition);
  }

 protected:
  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(RoamuxTabStripPositionSettingsFlagOffTest,
                       RowAbsentWhenFlagOff) {
  EXPECT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL("chrome://settings/appearance")));
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  // Wait for the page to be interactive via a stock anchor…
  EXPECT_EQ(true,
            content::EvalJs(web_contents, WaitForIdScript("themeRow", 100)));
  // …then assert the roamux row was never stamped (single settle pass).
  EXPECT_EQ(false,
            content::EvalJs(web_contents,
                            WaitForIdScript("roamuxTabStripPosition", 5)));
}

// roam-183: the upstream #tabStripPosition row is gated on
// showVerticalTabsEnabled = tabs::IsVerticalTabsFeatureEnabled(), so these
// fixtures enable the upstream feature to make the row present-by-default; the
// guard's job is to hide it when the roamux flag is also on.

// Flag ON + upstream vertical-tabs feature ON: the roamux row is present and
// the upstream row is suppressed (roam-183).
class RoamuxHideUpstreamRowTest : public roamux::test::RoamuxBrowserTest {
 public:
  RoamuxHideUpstreamRowTest() {
    features_.InitWithFeatures(
        {features::kTabStripPosition, ::tabs::kVerticalTabs}, {});
  }

 protected:
  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(RoamuxHideUpstreamRowTest,
                       UpstreamRowHiddenWhileRoamuxRowShown) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL("chrome://settings/appearance")));
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_EQ(true,
            content::EvalJs(web_contents, WaitForIdScript("themeRow", 100)));
  // The roamux row is present…
  EXPECT_EQ(true,
            content::EvalJs(web_contents,
                            WaitForIdScript("roamuxTabStripPosition", 100)));
  // …and the upstream Vertical/Horizontal row is suppressed.
  EXPECT_EQ(false, content::EvalJs(web_contents,
                                   WaitForIdScript("tabStripPosition", 5)));
}

// Flag OFF + upstream vertical-tabs feature ON: stock — the upstream row is
// present and the roamux row absent.
class RoamuxUpstreamRowStockTest : public roamux::test::RoamuxBrowserTest {
 public:
  RoamuxUpstreamRowStockTest() {
    features_.InitWithFeatures({::tabs::kVerticalTabs},
                               {features::kTabStripPosition});
  }

 protected:
  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(RoamuxUpstreamRowStockTest, UpstreamRowPresentStock) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL("chrome://settings/appearance")));
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_EQ(true,
            content::EvalJs(web_contents, WaitForIdScript("themeRow", 100)));
  EXPECT_EQ(true, content::EvalJs(web_contents,
                                  WaitForIdScript("tabStripPosition", 100)));
  EXPECT_EQ(false,
            content::EvalJs(web_contents,
                            WaitForIdScript("roamuxTabStripPosition", 5)));
}

}  // namespace
}  // namespace roamux

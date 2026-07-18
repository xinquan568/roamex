// SPDX-License-Identifier: Apache-2.0
// roam-140: runs the chrome://settings/help Roamux About WebUI mocha tests —
// the relocated <roamux-update-card> element suite (roamux_update_card_test.ts)
// against a TS-side fake page handler, and the branded about_page integration
// suite (roamux_settings_about_test.ts). The page is loaded under the settings
// host so the element's chrome://settings/roamux_about/* imports and the
// settings-about-page module resolve. Replaces the retired
// roamux_about_browsertest.cc (roam-37).

#include "base/strings/stringprintf.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/web_ui_mocha_browser_test.h"
#include "content/public/test/browser_test.h"
#include "roamux/test/support/roamux_browser_test.h"

class RoamuxSettingsAboutBrowserTest : public WebUIMochaBrowserTest {
 public:
  RoamuxSettingsAboutBrowserTest() {
    set_test_loader_host(chrome::kChromeUISettingsHost);
    // roam-99: foreign hierarchy (WebUIMochaBrowserTest, hosted in upstream
    // browser_tests) — fixture-owned ScopedFeatureList via the header-only
    // helper instead of re-basing.
    roamux::test::DisableWebUIToolbarFeatures(webui_toolbar_disables_);
  }

 protected:
  void RunCardTestCase(const std::string& test_case) {
    RunTest("roamux_settings_about/roamux_update_card_test.js",
            base::StringPrintf("runMochaTest('RoamuxUpdateCard', '%s');",
                               test_case.c_str()));
  }

  void RunAboutTestCase(const std::string& test_case) {
    RunTest("roamux_settings_about/roamux_settings_about_test.js",
            base::StringPrintf("runMochaTest('RoamuxSettingsAbout', '%s');",
                               test_case.c_str()));
  }

  // roam-157: the settings left-nav About chip (settings-menu element).
  void RunMenuTestCase(const std::string& test_case) {
    RunTest("roamux_settings_about/roamux_settings_menu_test.js",
            base::StringPrintf("runMochaTest('RoamuxSettingsMenu', '%s');",
                               test_case.c_str()));
  }

 private:
  base::test::ScopedFeatureList webui_toolbar_disables_;
};

// --- <roamux-update-card> element suite ---

IN_PROC_BROWSER_TEST_F(RoamuxSettingsAboutBrowserTest, NoConfigGroups) {
  RunCardTestCase("no configuration or reset groups present");
}

IN_PROC_BROWSER_TEST_F(RoamuxSettingsAboutBrowserTest, AvailableShowsCard) {
  RunCardTestCase("available shows card with download and skip");
}

IN_PROC_BROWSER_TEST_F(RoamuxSettingsAboutBrowserTest, DownloadProgressRestart) {
  RunCardTestCase("download then progress then restart");
}

IN_PROC_BROWSER_TEST_F(RoamuxSettingsAboutBrowserTest, SkipHidesCard) {
  RunCardTestCase("skip hides the card");
}

IN_PROC_BROWSER_TEST_F(RoamuxSettingsAboutBrowserTest, CheckNow) {
  RunCardTestCase("check now issues a check");
}

IN_PROC_BROWSER_TEST_F(RoamuxSettingsAboutBrowserTest, UpdatesUnavailable) {
  RunCardTestCase("updates unavailable renders the static state");
}

// --- branded chrome://settings/help integration suite ---

IN_PROC_BROWSER_TEST_F(RoamuxSettingsAboutBrowserTest, EmbedsUpdateCard) {
  RunAboutTestCase("embeds the roamux update card");
}

IN_PROC_BROWSER_TEST_F(RoamuxSettingsAboutBrowserTest, BrandedLogoAndTitle) {
  RunAboutTestCase("branded logo and title render");
}

IN_PROC_BROWSER_TEST_F(RoamuxSettingsAboutBrowserTest, LinksResolveToGitHub) {
  RunAboutTestCase("website and github links both resolve to the GitHub repo");
}

// roam-156 left this mocha case without a C++ runner, so it never executed. roam-157
// touches the same About surface (hero logo -> .svg); wire it in while here.
IN_PROC_BROWSER_TEST_F(RoamuxSettingsAboutBrowserTest, AboutVersionLines) {
  RunAboutTestCase("version lines name Roamux and Chromium separately");
}


// --- settings nav About chip suite (roam-157) ---

IN_PROC_BROWSER_TEST_F(RoamuxSettingsAboutBrowserTest, MenuChipGlyph) {
  RunMenuTestCase("about chip shows the roamux glyph");
}

IN_PROC_BROWSER_TEST_F(RoamuxSettingsAboutBrowserTest, MenuChipTwoLines) {
  RunMenuTestCase("about chip renders two lines: title over v-version");
}

IN_PROC_BROWSER_TEST_F(RoamuxSettingsAboutBrowserTest, MenuChipSiblingsSingleLine) {
  RunMenuTestCase("other nav items keep a single line");
}

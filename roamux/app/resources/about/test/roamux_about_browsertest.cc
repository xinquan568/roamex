// SPDX-License-Identifier: Apache-2.0
// roam-37: runs the chrome://roamux-about WebUI mocha tests (about_test.ts)
// against the TS-side fake page handler. The page is registered internal-only
// (DefaultInternalWebUIConfig), so the test enables internal UIs first.

#include "base/strings/stringprintf.h"
#include "chrome/browser/browser_process.h"
#include "chrome/test/base/web_ui_mocha_browser_test.h"
#include "components/prefs/pref_service.h"
#include "components/webui/chrome_urls/pref_names.h"
#include "content/public/test/browser_test.h"

class RoamuxAboutBrowserTest : public WebUIMochaBrowserTest {
public:
  RoamuxAboutBrowserTest() { set_test_loader_host("roamux-about"); }

protected:
  void RunTestCase(const std::string &test_case) {
    g_browser_process->local_state()->SetBoolean(
        chrome_urls::kInternalOnlyUisEnabled, true);
    RunTest("roamux_about/about_test.js",
            base::StringPrintf("runMochaTest('RoamuxAbout', '%s');",
                               test_case.c_str()));
  }
};

IN_PROC_BROWSER_TEST_F(RoamuxAboutBrowserTest, IdentityAndLinks) {
  RunTestCase("identity and links render");
}

IN_PROC_BROWSER_TEST_F(RoamuxAboutBrowserTest, NoConfigGroups) {
  RunTestCase("no configuration or reset groups present");
}

IN_PROC_BROWSER_TEST_F(RoamuxAboutBrowserTest, AvailableShowsCard) {
  RunTestCase("available shows card with download and skip");
}

IN_PROC_BROWSER_TEST_F(RoamuxAboutBrowserTest, DownloadProgressRestart) {
  RunTestCase("download then progress then restart");
}

IN_PROC_BROWSER_TEST_F(RoamuxAboutBrowserTest, SkipHidesCard) {
  RunTestCase("skip hides the card");
}

IN_PROC_BROWSER_TEST_F(RoamuxAboutBrowserTest, CheckNow) {
  RunTestCase("check now issues a check");
}

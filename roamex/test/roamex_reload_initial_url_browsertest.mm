// SPDX-License-Identifier: Apache-2.0
// roam-12 (I-2.3): IDC_RELOAD_INITIAL_URL fires (and nothing else), is
// disabled on NTP/blank and with the flag off, stays fresh across tab
// switches, and the Ctrl+Cmd+R mapping exists (mac).
// (TDD: written RED before patch 0010.)

#include "base/run_loop.h"
#include "base/test/scoped_feature_list.h"
#include "build/build_config.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_command_controller.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "roamex/browser/tabs/tab_initial_url_helper.h"
#include "roamex/common/roamex_features.h"
#include "roamex/test/support/sso_test_server.h"

#if BUILDFLAG(IS_MAC)
#include "chrome/browser/ui/cocoa/accelerators_cocoa.h"
#include "ui/base/accelerators/accelerator.h"
#endif

namespace roamex {
namespace {

class RoamexReloadInitialUrlTest : public InProcessBrowserTest {
 public:
  RoamexReloadInitialUrlTest() {
    features_.InitAndEnableFeature(features::kInitialUrl);
  }

  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    InProcessBrowserTest::SetUpOnMainThread();
  }

 protected:
  content::WebContents* active_contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  chrome::BrowserCommandController* command_controller() {
    return browser()->GetFeatures().browser_command_controller();
  }

  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(RoamexReloadInitialUrlTest,
                       FiresAndReturnsToInitialUrl) {
  test::SsoTestServer sso;
  ASSERT_TRUE(sso.Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), sso.landing_url()));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), sso.idp_page_url()));
  EXPECT_EQ(sso.idp_page_url(), active_contents()->GetLastCommittedURL());

  EXPECT_TRUE(command_controller()->IsCommandEnabled(IDC_RELOAD_INITIAL_URL));
  ASSERT_TRUE(command_controller()->ExecuteCommand(IDC_RELOAD_INITIAL_URL));
  ASSERT_TRUE(content::WaitForLoadStop(active_contents()));
  EXPECT_EQ(sso.landing_url(), active_contents()->GetLastCommittedURL());
  // "and nothing else": the captured value is untouched (sticky).
  EXPECT_EQ(sso.landing_url(),
            tabs::TabInitialUrlHelper::FromWebContents(active_contents())
                ->initial_url());
}

IN_PROC_BROWSER_TEST_F(RoamexReloadInitialUrlTest,
                       DisabledOnFreshTabAndFreshAcrossSwitches) {
  test::SsoTestServer sso;
  ASSERT_TRUE(sso.Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), sso.landing_url()));
  EXPECT_TRUE(command_controller()->IsCommandEnabled(IDC_RELOAD_INITIAL_URL));

  // A fresh NTP tab has no initial URL -> disabled.
  chrome::NewTab(browser());
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(command_controller()->IsCommandEnabled(IDC_RELOAD_INITIAL_URL));

  // Switching back re-enables (freshness across tab switches).
  browser()->tab_strip_model()->ActivateTabAt(0);
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(command_controller()->IsCommandEnabled(IDC_RELOAD_INITIAL_URL));
}

#if BUILDFLAG(IS_MAC)
IN_PROC_BROWSER_TEST_F(RoamexReloadInitialUrlTest, AcceleratorMappingExists) {
  AcceleratorsCocoa* accelerators = AcceleratorsCocoa::GetInstance();
  const ui::Accelerator* accelerator =
      accelerators->GetAcceleratorForCommand(IDC_RELOAD_INITIAL_URL);
  ASSERT_NE(nullptr, accelerator);
  EXPECT_EQ(ui::VKEY_R, accelerator->key_code());
  EXPECT_EQ(ui::EF_COMMAND_DOWN | ui::EF_CONTROL_DOWN,
            accelerator->modifiers());
}
#endif

class RoamexReloadInitialUrlFlagOffTest : public InProcessBrowserTest {
 public:
  RoamexReloadInitialUrlFlagOffTest() {
    features_.InitAndDisableFeature(features::kInitialUrl);
  }

 protected:
  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(RoamexReloadInitialUrlFlagOffTest, DisabledWhenFlagOff) {
  EXPECT_FALSE(
      browser()->GetFeatures().browser_command_controller()->IsCommandEnabled(
          IDC_RELOAD_INITIAL_URL));
}

}  // namespace
}  // namespace roamex

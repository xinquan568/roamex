// SPDX-License-Identifier: Apache-2.0
// roam-14 (I-2.5, §4.7): edit round-trip (menu delegate + dialog seam),
// set-to-current, duplicate inherits value+lock (uid re-mints), and malformed
// persisted data is survived. Session-restore coverage is the PRE_ pair in
// roamux_initial_url_restore_browsertest.cc.

#include "base/run_loop.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "roamux/browser/tabs/tab_initial_url_helper.h"
#include "roamux/browser/tabs/tab_uid_tab_helper.h"
#include "roamux/browser/ui/tabs/edit_initial_url_dialog.h"
#include "roamux/browser/ui/tabs/initial_url_menu.h"
#include "roamux/common/roamux_features.h"
#include "ui/menus/simple_menu_model.h"

namespace roamux {
namespace {

class RoamuxInitialUrlEditTest : public InProcessBrowserTest {
 public:
  RoamuxInitialUrlEditTest() {
    features_.InitAndEnableFeature(features::kInitialUrl);
  }

  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
    InProcessBrowserTest::SetUpOnMainThread();
  }

 protected:
  content::WebContents* active_contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }
  tabs::TabInitialUrlHelper* helper() {
    return tabs::TabInitialUrlHelper::FromWebContents(active_contents());
  }

  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(RoamuxInitialUrlEditTest, EditDialogSeamLocksValue) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  // Edit through the real validate+commit seam.
  ASSERT_TRUE(tabs::SubmitEditInitialUrlForTesting(active_contents(),
                                                   "https://edited.test/"));
  ASSERT_TRUE(helper()->has_initial_url());
  EXPECT_EQ(GURL("https://edited.test/"), helper()->initial_url());
  EXPECT_TRUE(helper()->is_user_locked());

  // A later navigation (would-be capture) must not overwrite the locked value.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title2.html")));
  EXPECT_EQ(GURL("https://edited.test/"), helper()->initial_url());

  // An invalid edit is a no-op.
  EXPECT_FALSE(
      tabs::SubmitEditInitialUrlForTesting(active_contents(), "not a url"));
  EXPECT_EQ(GURL("https://edited.test/"), helper()->initial_url());
}

IN_PROC_BROWSER_TEST_F(RoamuxInitialUrlEditTest,
                       DuplicateInheritsValueAndLock) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  ASSERT_TRUE(tabs::SubmitEditInitialUrlForTesting(active_contents(),
                                                   "https://edited.test/"));
  const std::string source_uid =
      tabs::TabUidTabHelper::FromWebContents(active_contents())->uid();

  chrome::DuplicateTab(browser());
  content::WebContents* dupe = active_contents();
  tabs::TabInitialUrlHelper* dupe_helper =
      tabs::TabInitialUrlHelper::FromWebContents(dupe);
  ASSERT_NE(nullptr, dupe_helper);
  // §4.2: value+lock inherited…
  EXPECT_EQ(GURL("https://edited.test/"), dupe_helper->initial_url());
  EXPECT_TRUE(dupe_helper->is_user_locked());
  // …but the durable uid re-mints (roam-10 uniqueness rule).
  EXPECT_NE(source_uid, tabs::TabUidTabHelper::FromWebContents(dupe)->uid());
}

IN_PROC_BROWSER_TEST_F(RoamuxInitialUrlEditTest, SetToCurrentPageLocks) {
  const GURL page_b = embedded_test_server()->GetURL("/title2.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_b));
  // The menu "Set initial URL to current page" path: current committed URL
  // through SetUserInitialUrl (locks). Drive the helper as the delegate does.
  tabs::TabInitialUrlHelper::MaybeCreateForWebContents(active_contents());
  helper()->SetUserInitialUrl(active_contents()->GetLastCommittedURL());
  EXPECT_EQ(page_b, helper()->initial_url());
  EXPECT_TRUE(helper()->is_user_locked());
}

IN_PROC_BROWSER_TEST_F(RoamuxInitialUrlEditTest, ReopenClosedRestoresValue) {
  ASSERT_TRUE(AddTabAtIndex(1, embedded_test_server()->GetURL("/title1.html"),
                            ui::PAGE_TRANSITION_TYPED));
  content::WebContents* tab = browser()->tab_strip_model()->GetWebContentsAt(1);
  ASSERT_TRUE(
      tabs::SubmitEditInitialUrlForTesting(tab, "https://edited.test/"));

  content::WebContentsDestroyedWatcher destroyed(tab);
  browser()->tab_strip_model()->CloseWebContentsAt(
      1, TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
  destroyed.Wait();

  // Reopen through the real TabRestoreService hand-off (the 0009 channel).
  chrome::RestoreTab(browser());
  base::RunLoop().RunUntilIdle();
  ASSERT_EQ(2, browser()->tab_strip_model()->count());
  tabs::TabInitialUrlHelper* reopened =
      tabs::TabInitialUrlHelper::FromWebContents(
          browser()->tab_strip_model()->GetWebContentsAt(1));
  ASSERT_NE(nullptr, reopened);
  EXPECT_EQ(GURL("https://edited.test/"), reopened->initial_url());
  EXPECT_TRUE(reopened->is_user_locked());
}

IN_PROC_BROWSER_TEST_F(RoamuxInitialUrlEditTest,
                       MalformedPersistedDataSurvives) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  content::WebContents::CreateParams params(browser()->profile());
  std::unique_ptr<content::WebContents> restored =
      content::WebContents::Create(params);
  content::WebContents* raw = restored.get();
  // Inject garbage extra data through the real consumer: restore must not
  // crash and the tab stays uncaptured.
  tabs::TabInitialUrlHelper::SetPendingRestoredInitialUrl(
      raw, {{tabs::TabInitialUrlHelper::kExtraDataKey, "@@garbage@@"}});
  browser()->tab_strip_model()->AppendWebContents(std::move(restored),
                                                  /*foreground=*/true);
  base::RunLoop().RunUntilIdle();
  tabs::TabInitialUrlHelper* h =
      tabs::TabInitialUrlHelper::FromWebContents(raw);
  if (h) {
    EXPECT_FALSE(h->has_initial_url());
  }
}

IN_PROC_BROWSER_TEST_F(RoamuxInitialUrlEditTest,
                       SubMenuAppearsAndDrivesActions) {
  // The submenu appears (flag on) and carries the two §4.5 actions.
  ui::SimpleMenuModel parent(nullptr);
  std::unique_ptr<ui::SimpleMenuModel> submenu =
      tabs::MaybeAppendInitialUrlSubMenu(&parent, active_contents());
  ASSERT_NE(nullptr, submenu);
  ASSERT_EQ(2u, submenu->GetItemCount());

  // On about:blank, "Set to current page" (index 1) is disabled…
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("about:blank")));
  {
    ui::SimpleMenuModel parent_blank(nullptr);
    std::unique_ptr<ui::SimpleMenuModel> blank_menu =
        tabs::MaybeAppendInitialUrlSubMenu(&parent_blank, active_contents());
    ASSERT_NE(nullptr, blank_menu);
    EXPECT_FALSE(blank_menu->IsEnabledAt(1));
  }

  // …and enabled on a real page, where activating it writes + locks.
  const GURL page = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page));
  ui::SimpleMenuModel parent_live(nullptr);
  std::unique_ptr<ui::SimpleMenuModel> live_menu =
      tabs::MaybeAppendInitialUrlSubMenu(&parent_live, active_contents());
  ASSERT_NE(nullptr, live_menu);
  EXPECT_TRUE(live_menu->IsEnabledAt(1));
  live_menu->ActivatedAt(1);  // ExecuteCommand(set-to-current) via the model.
  ASSERT_TRUE(helper()->has_initial_url());
  EXPECT_EQ(page, helper()->initial_url());
  EXPECT_TRUE(helper()->is_user_locked());
}

class RoamuxInitialUrlEditFlagOffTest : public InProcessBrowserTest {
 public:
  RoamuxInitialUrlEditFlagOffTest() {
    features_.InitAndDisableFeature(features::kInitialUrl);
  }

 protected:
  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(RoamuxInitialUrlEditFlagOffTest, NoHelperWhenFlagOff) {
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  // The set-to-current/edit seams no-op: no helper is created.
  EXPECT_FALSE(
      tabs::SubmitEditInitialUrlForTesting(contents, "https://edited.test/"));
  EXPECT_EQ(nullptr, tabs::TabInitialUrlHelper::FromWebContents(contents));
}

IN_PROC_BROWSER_TEST_F(RoamuxInitialUrlEditFlagOffTest, NoSubmenuWhenFlagOff) {
  ui::SimpleMenuModel parent(nullptr);
  EXPECT_EQ(nullptr,
            tabs::MaybeAppendInitialUrlSubMenu(
                &parent, browser()->tab_strip_model()->GetActiveWebContents()));
}

}  // namespace
}  // namespace roamux

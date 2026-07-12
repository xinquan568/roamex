// SPDX-License-Identifier: Apache-2.0
// roam-10 (I-2.1) mandatory matrix (plan D6): live uniqueness, duplicate
// mints fresh, TabRestoreService reopen reuses through the real hand-off,
// crafted-collision re-stamps, flag-off inert, OTR isolated and unpersisted.
// (TDD: written RED against patch 0009.)

#include <map>
#include <string>

#include "base/run_loop.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tab_list/tab_list_interface.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/test_utils.h"
#include "roamux/browser/tabs/tab_uid_service.h"
#include "roamux/browser/tabs/tab_uid_service_factory.h"
#include "roamux/browser/tabs/tab_uid_tab_helper.h"
#include "roamux/common/roamux_features.h"
#include "url/gurl.h"

namespace roamux {
namespace {

class RoamuxTabUidTest : public InProcessBrowserTest {
 public:
  RoamuxTabUidTest() { features_.InitAndEnableFeature(features::kInitialUrl); }

 protected:
  tabs::TabUidService* service() {
    return tabs::TabUidServiceFactory::GetForProfile(browser()->profile());
  }

  std::string UidOfTabAt(int index) {
    content::WebContents* web_contents =
        browser()->tab_strip_model()->GetWebContentsAt(index);
    tabs::TabUidTabHelper* helper =
        tabs::TabUidTabHelper::FromWebContents(web_contents);
    return helper ? helper->uid() : std::string();
  }

  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(RoamuxTabUidTest, EveryLiveTabHasADistinctUid) {
  ASSERT_TRUE(AddTabAtIndex(1, GURL("about:blank"), ui::PAGE_TRANSITION_TYPED));
  ASSERT_TRUE(AddTabAtIndex(2, GURL("about:blank"), ui::PAGE_TRANSITION_TYPED));
  const std::string uid0 = UidOfTabAt(0);
  const std::string uid1 = UidOfTabAt(1);
  const std::string uid2 = UidOfTabAt(2);
  EXPECT_FALSE(uid0.empty());
  EXPECT_FALSE(uid1.empty());
  EXPECT_FALSE(uid2.empty());
  EXPECT_NE(uid0, uid1);
  EXPECT_NE(uid1, uid2);
  EXPECT_NE(uid0, uid2);
}

IN_PROC_BROWSER_TEST_F(RoamuxTabUidTest, DuplicateMintsFresh) {
  const std::string original = UidOfTabAt(0);
  ASSERT_FALSE(original.empty());
  chrome::DuplicateTab(browser());
  ASSERT_EQ(2, browser()->tab_strip_model()->count());
  const std::string duplicate = UidOfTabAt(1);
  EXPECT_FALSE(duplicate.empty());
  EXPECT_NE(original, duplicate);
  EXPECT_TRUE(service()->IsLive(original));
  EXPECT_TRUE(service()->IsLive(duplicate));
}

IN_PROC_BROWSER_TEST_F(RoamuxTabUidTest, ReopenReusesTheClosedTabsUid) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("about:blank")));
  ASSERT_TRUE(AddTabAtIndex(1, GURL("about:blank"), ui::PAGE_TRANSITION_TYPED));
  const std::string uid = UidOfTabAt(1);
  ASSERT_FALSE(uid.empty());

  content::WebContentsDestroyedWatcher destroyed(
      browser()->tab_strip_model()->GetWebContentsAt(1));
  browser()->tab_strip_model()->CloseWebContentsAt(
      1, TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
  destroyed.Wait();
  EXPECT_FALSE(service()->IsLive(uid));

  // Reopen via TabRestoreService — flows through the real 0009 hand-off.
  chrome::RestoreTab(browser());
  base::RunLoop().RunUntilIdle();
  ASSERT_EQ(2, browser()->tab_strip_model()->count());
  EXPECT_EQ(uid, UidOfTabAt(1));
  EXPECT_TRUE(service()->IsLive(uid));
}

IN_PROC_BROWSER_TEST_F(RoamuxTabUidTest, LiveCollisionRestamps) {
  const std::string live_uid = UidOfTabAt(0);
  ASSERT_FALSE(live_uid.empty());

  // Craft a "restored" tab carrying a uid that is ALREADY live (§6.9 startup
  // collision), driving the real hand-off + attach path.
  content::WebContents::CreateParams create_params(browser()->profile());
  std::unique_ptr<content::WebContents> web_contents =
      content::WebContents::Create(create_params);
  tabs::TabUidTabHelper::SetPendingRestoredUid(
      web_contents.get(), {{tabs::TabUidTabHelper::kExtraDataKey, live_uid}});
  browser()->tab_strip_model()->AppendWebContents(std::move(web_contents),
                                                  /*foreground=*/true);
  base::RunLoop().RunUntilIdle();

  const std::string restamped = UidOfTabAt(1);
  EXPECT_FALSE(restamped.empty());
  EXPECT_NE(live_uid, restamped);
  EXPECT_EQ(live_uid, UidOfTabAt(0)) << "the incumbent keeps its identity";
}

IN_PROC_BROWSER_TEST_F(RoamuxTabUidTest, OtrIsIsolatedAndUnpersisted) {
  Browser* incognito = CreateIncognitoBrowser();
  tabs::TabUidService* otr_service =
      tabs::TabUidServiceFactory::GetForProfile(incognito->profile());
  ASSERT_NE(nullptr, otr_service);
  EXPECT_NE(service(), otr_service) << "OTR must not redirect (D5)";

  content::WebContents* otr_contents =
      incognito->tab_strip_model()->GetWebContentsAt(0);
  tabs::TabUidTabHelper* helper =
      tabs::TabUidTabHelper::FromWebContents(otr_contents);
  ASSERT_NE(nullptr, helper);
  EXPECT_FALSE(helper->uid().empty());
  EXPECT_TRUE(otr_service->IsLive(helper->uid()));
  EXPECT_FALSE(service()->IsLive(helper->uid()))
      << "OTR identities never enter the regular registry";
}

IN_PROC_BROWSER_TEST_F(RoamuxTabUidTest, DiscardKeepsTheSameUid) {
  // A background tab (the active tab cannot be discarded).
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL("about:blank"), WindowOpenDisposition::NEW_BACKGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  ASSERT_EQ(2, browser()->tab_strip_model()->count());
  const std::string uid = UidOfTabAt(1);
  ASSERT_FALSE(uid.empty());

  // Drive the canonical discard flow — the same live tab gets replacement
  // contents; identity must survive without a re-stamp.
  TabListInterface* tab_list = TabListInterface::From(browser());
  ASSERT_NE(nullptr, tab_list);
  ASSERT_NE(nullptr, tab_list->DiscardTab(tab_list->GetTab(1)->GetHandle()));
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(uid, UidOfTabAt(1));
  EXPECT_TRUE(service()->IsLive(uid));
}

class RoamuxTabUidFlagOffTest : public InProcessBrowserTest {
 public:
  RoamuxTabUidFlagOffTest() {
    features_.InitAndDisableFeature(features::kInitialUrl);
  }

 protected:
  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(RoamuxTabUidFlagOffTest, NoHelperNoUidWhenFlagOff) {
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetWebContentsAt(0);
  EXPECT_EQ(nullptr, tabs::TabUidTabHelper::FromWebContents(web_contents));
}

}  // namespace
}  // namespace roamux

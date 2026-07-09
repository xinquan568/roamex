// SPDX-License-Identifier: Apache-2.0
// roam-25 (I-4.5) F3 flag-matrix: E4 traversal keys on the durable tab uid, so
// the uid helper must be created under kTabVisitNav even when kInitialUrl is
// OFF. Otherwise a kTabVisitNav-only build would have no durable identities and
// the roam-24 integration gate would be violated.

#include <string>

#include "base/test/scoped_feature_list.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "roamex/browser/tabs/tab_uid_tab_helper.h"
#include "roamex/common/roamex_features.h"
#include "url/gurl.h"

namespace roamex {
namespace {

class RoamexTabVisitUidFlagMatrixTest : public InProcessBrowserTest {
 public:
  RoamexTabVisitUidFlagMatrixTest() {
    // The E4 flag is ON, but the E2 initial-url flag is OFF — the durable uid
    // must still exist for traversal.
    features_.InitWithFeatures(/*enabled=*/{features::kTabVisitNav},
                               /*disabled=*/{features::kInitialUrl});
  }

 protected:
  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(RoamexTabVisitUidFlagMatrixTest,
                       EveryTabHasDurableUidUnderTabVisitNavOnly) {
  ASSERT_TRUE(AddTabAtIndex(1, GURL("about:blank"), ui::PAGE_TRANSITION_TYPED));
  ASSERT_TRUE(AddTabAtIndex(2, GURL("about:blank"), ui::PAGE_TRANSITION_TYPED));

  TabStripModel* tabs = browser()->tab_strip_model();
  for (int i = 0; i < tabs->count(); ++i) {
    tabs::TabUidTabHelper* helper =
        tabs::TabUidTabHelper::FromWebContents(tabs->GetWebContentsAt(i));
    ASSERT_TRUE(helper) << "no uid helper on tab " << i;
    EXPECT_FALSE(helper->uid().empty()) << "empty uid on tab " << i;
  }
}

}  // namespace
}  // namespace roamex

// SPDX-License-Identifier: Apache-2.0
// roam-7 (I-1.2): the live-switch observer — a placement-pref change must fire
// the layout-invalidation closure (TDD: RED before the implementation).

#include "roamex/browser/ui/tabs/tab_strip_placement_observer.h"

#include "base/functional/callback.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "roamex/common/roamex_prefs.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace roamex::tabs {
namespace {

class TabStripPlacementObserverTest : public testing::Test {
 protected:
  TabStripPlacementObserverTest() {
    prefs::RegisterProfilePrefs(pref_service_.registry());
  }

  TestingPrefServiceSimple pref_service_;
};

TEST_F(TabStripPlacementObserverTest, FiresOnPlacementPrefChange) {
  int fired = 0;
  TabStripPlacementObserver observer(
      &pref_service_,
      base::BindRepeating([](int* fired) { ++*fired; }, &fired));
  pref_service_.SetInteger(prefs::kTabStripPosition, 1);
  EXPECT_EQ(1, fired);
  pref_service_.SetInteger(prefs::kTabStripPosition, 2);
  EXPECT_EQ(2, fired);
}

TEST_F(TabStripPlacementObserverTest, DoesNotFireForOtherPrefs) {
  int fired = 0;
  TabStripPlacementObserver observer(
      &pref_service_,
      base::BindRepeating([](int* fired) { ++*fired; }, &fired));
  pref_service_.SetBoolean(prefs::kReopenClosed, true);
  EXPECT_EQ(0, fired);
}

TEST_F(TabStripPlacementObserverTest, StopsFiringAfterDestruction) {
  int fired = 0;
  {
    TabStripPlacementObserver observer(
        &pref_service_,
        base::BindRepeating([](int* fired) { ++*fired; }, &fired));
  }
  pref_service_.SetInteger(prefs::kTabStripPosition, 3);
  EXPECT_EQ(0, fired);
}

}  // namespace
}  // namespace roamex::tabs

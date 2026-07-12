// SPDX-License-Identifier: Apache-2.0
// roam-6 (I-1.1): the tab-strip context-menu position submenu — flag gating,
// radio state from the pref, and pref writes on activation (TDD/P6: written RED
// before the implementation).

#include "roamux/browser/ui/tabs/tab_strip_position_menu.h"

#include <memory>

#include "base/test/scoped_feature_list.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "roamux/common/roamux_features.h"
#include "roamux/common/roamux_prefs.h"
#include "roamux/common/tab_strip_placement.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/menus/simple_menu_model.h"

namespace roamux::tabs {
namespace {

class TabStripPositionMenuTest : public testing::Test {
 protected:
  TabStripPositionMenuTest() : parent_(/*delegate=*/nullptr) {
    prefs::RegisterProfilePrefs(pref_service_.registry());
  }

  TestingPrefServiceSimple pref_service_;
  ui::SimpleMenuModel parent_;
};

TEST_F(TabStripPositionMenuTest, FlagOffAppendsNothing) {
  base::test::ScopedFeatureList features;
  features.InitAndDisableFeature(features::kTabStripPosition);
  parent_.AddItem(1, u"existing item");  // Mirrors the real non-empty menu.
  std::unique_ptr<ui::SimpleMenuModel> submenu =
      MaybeAppendTabStripPositionSubMenu(&parent_, &pref_service_);
  EXPECT_EQ(nullptr, submenu);
  EXPECT_EQ(1u, parent_.GetItemCount());
}

TEST_F(TabStripPositionMenuTest, FlagOnAppendsSubmenuWithFourRadioItems) {
  base::test::ScopedFeatureList features;
  features.InitAndEnableFeature(features::kTabStripPosition);
  parent_.AddItem(1, u"existing item");  // Mirrors the real non-empty menu
                                         // (a leading separator is elided).
  std::unique_ptr<ui::SimpleMenuModel> submenu =
      MaybeAppendTabStripPositionSubMenu(&parent_, &pref_service_);
  ASSERT_NE(nullptr, submenu);
  // Parent gains a separator + the submenu entry.
  EXPECT_EQ(3u, parent_.GetItemCount());
  EXPECT_EQ(ui::MenuModel::TYPE_SUBMENU,
            parent_.GetTypeAt(parent_.GetItemCount() - 1));
  ASSERT_EQ(4u, submenu->GetItemCount());
  for (size_t i = 0; i < 4u; ++i) {
    EXPECT_EQ(ui::MenuModel::TYPE_RADIO, submenu->GetTypeAt(i));
  }
}

TEST_F(TabStripPositionMenuTest, CheckedItemMirrorsThePref) {
  base::test::ScopedFeatureList features;
  features.InitAndEnableFeature(features::kTabStripPosition);
  SetTabStripPlacement(&pref_service_, TabStripPlacement::kLeft);
  std::unique_ptr<ui::SimpleMenuModel> submenu =
      MaybeAppendTabStripPositionSubMenu(&parent_, &pref_service_);
  ASSERT_NE(nullptr, submenu);
  // Items are ordered Top, Bottom, Left, Right.
  EXPECT_FALSE(submenu->IsItemCheckedAt(0));
  EXPECT_FALSE(submenu->IsItemCheckedAt(1));
  EXPECT_TRUE(submenu->IsItemCheckedAt(2));
  EXPECT_FALSE(submenu->IsItemCheckedAt(3));
}

TEST_F(TabStripPositionMenuTest, ActivatingAnItemWritesThePref) {
  base::test::ScopedFeatureList features;
  features.InitAndEnableFeature(features::kTabStripPosition);
  std::unique_ptr<ui::SimpleMenuModel> submenu =
      MaybeAppendTabStripPositionSubMenu(&parent_, &pref_service_);
  ASSERT_NE(nullptr, submenu);
  submenu->ActivatedAt(1);  // Bottom.
  EXPECT_EQ(TabStripPlacement::kBottom, GetTabStripPlacement(&pref_service_));
  submenu->ActivatedAt(3);  // Right.
  EXPECT_EQ(TabStripPlacement::kRight, GetTabStripPlacement(&pref_service_));
}

}  // namespace
}  // namespace roamux::tabs

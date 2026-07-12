// SPDX-License-Identifier: Apache-2.0
// roam-10 (I-2.1): the TabUidService registry — §6.2 live-uniqueness and the
// §6.9 adopt-or-restamp rules (TDD: written RED before the implementation).

#include "roamux/browser/tabs/tab_uid_service.h"

#include "components/sessions/core/session_id.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace roamux::tabs {
namespace {

class TabUidServiceTest : public testing::Test {
 protected:
  TabUidService service_;
  const SessionID tab_a_ = SessionID::NewUnique();
  const SessionID tab_b_ = SessionID::NewUnique();
};

TEST_F(TabUidServiceTest, MintsUniqueUidsPerLiveTab) {
  const std::string uid_a = service_.MintAndRegister(tab_a_);
  const std::string uid_b = service_.MintAndRegister(tab_b_);
  EXPECT_FALSE(uid_a.empty());
  EXPECT_FALSE(uid_b.empty());
  EXPECT_NE(uid_a, uid_b);
  EXPECT_EQ(uid_a, service_.GetUidForTab(tab_a_).value_or(""));
  EXPECT_EQ(uid_b, service_.GetUidForTab(tab_b_).value_or(""));
  EXPECT_TRUE(service_.IsLive(uid_a));
  EXPECT_TRUE(service_.IsLive(uid_b));
}

TEST_F(TabUidServiceTest, AdoptReusesAFreeRestoredUid) {
  const std::string restored = "11111111-2222-4333-8444-555555555555";
  const std::string effective = service_.AdoptOrRestamp(tab_a_, restored);
  EXPECT_EQ(restored, effective);
  EXPECT_TRUE(service_.IsLive(restored));
}

TEST_F(TabUidServiceTest, AdoptRestampsWhenRestoredUidIsLive) {
  const std::string uid_a = service_.MintAndRegister(tab_a_);
  // §6.9: a second tab arriving with the SAME persisted uid must get a fresh
  // one — the incumbent keeps its identity.
  const std::string effective = service_.AdoptOrRestamp(tab_b_, uid_a);
  EXPECT_NE(uid_a, effective);
  EXPECT_FALSE(effective.empty());
  EXPECT_EQ(uid_a, service_.GetUidForTab(tab_a_).value_or(""));
  EXPECT_EQ(effective, service_.GetUidForTab(tab_b_).value_or(""));
}

TEST_F(TabUidServiceTest, UnregisterFreesTheUidForReopenReuse) {
  const std::string uid_a = service_.MintAndRegister(tab_a_);
  service_.Unregister(tab_a_);
  EXPECT_FALSE(service_.IsLive(uid_a));
  EXPECT_FALSE(service_.GetUidForTab(tab_a_).has_value());
  // Reopen: the closed tab's persisted uid is adoptable again.
  EXPECT_EQ(uid_a, service_.AdoptOrRestamp(tab_b_, uid_a));
}

TEST_F(TabUidServiceTest, AdoptReplacesAnEarlierMintForTheSameTab) {
  // Ordering tolerance: if a tab was minted first and the restored uid
  // arrives later (hand-off after attach), adoption replaces the mint when
  // the restored uid is free.
  const std::string minted = service_.MintAndRegister(tab_a_);
  const std::string restored = "99999999-8888-4777-a666-555555555555";
  const std::string effective = service_.AdoptOrRestamp(tab_a_, restored);
  EXPECT_EQ(restored, effective);
  EXPECT_FALSE(service_.IsLive(minted));
  EXPECT_EQ(restored, service_.GetUidForTab(tab_a_).value_or(""));
}

TEST_F(TabUidServiceTest, RejectsMalformedRestoredUidWithFreshMint) {
  const std::string effective = service_.AdoptOrRestamp(tab_a_, "not-a-uuid");
  EXPECT_FALSE(effective.empty());
  EXPECT_NE("not-a-uuid", effective);
  EXPECT_TRUE(service_.IsLive(effective));
}

}  // namespace
}  // namespace roamux::tabs

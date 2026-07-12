// SPDX-License-Identifier: Apache-2.0
// roam-21 (I-4.1): the settled-visit journal service + factory — regular and
// OTR get separate instances, OTR never touches disk, the regular journal
// persists across reopen, and a disabled feature creates no journal DB. All
// RecordVisit- dependent assertions drain the store's SequenceBound (via
// GetVisits) first.

#include "roamux/browser/tab_visit/settled_visit_journal_service.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/test_future.h"
#include "chrome/browser/profiles/chrome_browser_main_extra_parts_profiles.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/browser_task_environment.h"
#include "roamux/browser/tab_visit/settled_visit_journal_factory.h"
#include "roamux/browser/tab_visit/visits_store.h"
#include "roamux/common/roamux_features.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace roamux::tab_visit {
namespace {

std::vector<VisitRow> GetVisitsSync(SettledVisitJournalService* service) {
  base::test::TestFuture<std::vector<VisitRow>> future;
  service->GetVisits(future.GetCallback());
  return future.Take();
}

// Drains the store sequence and returns (persisted tab state, volatile live
// id).
std::pair<std::optional<TabStateRow>, std::optional<std::string>>
GetTabStateSync(SettledVisitJournalService* service,
                const std::string& restore_key) {
  base::test::TestFuture<std::optional<TabStateRow>, std::optional<std::string>>
      future;
  service->GetTabState(restore_key, future.GetCallback());
  return {future.Get<0>(), future.Get<1>()};
}

class SettledVisitJournalServiceTestBase : public testing::Test {
 public:
  SettledVisitJournalServiceTestBase() {
    ChromeBrowserMainExtraPartsProfiles::
        EnsureBrowserContextKeyedServiceFactoriesBuilt();
  }

 protected:
  base::FilePath JournalPath(Profile* profile) {
    return profile->GetPath().Append(FILE_PATH_LITERAL("RoamuxTabVisits"));
  }

  content::BrowserTaskEnvironment task_environment_;
  TestingProfile profile_;
};

class SettledVisitJournalServiceTest
    : public SettledVisitJournalServiceTestBase {
 public:
  SettledVisitJournalServiceTest() {
    features_.InitAndEnableFeature(features::kTabVisitNav);
  }

 private:
  base::test::ScopedFeatureList features_;
};

TEST_F(SettledVisitJournalServiceTest, RegularAndOtrGetSeparateInstances) {
  Profile* otr = profile_.GetPrimaryOTRProfile(/*create_if_needed=*/true);
  SettledVisitJournalService* regular =
      SettledVisitJournalFactory::GetForProfile(&profile_);
  SettledVisitJournalService* otr_service =
      SettledVisitJournalFactory::GetForProfile(otr);
  ASSERT_TRUE(regular);
  ASSERT_TRUE(otr_service);
  EXPECT_NE(regular, otr_service);
}

TEST_F(SettledVisitJournalServiceTest, OtrRecordsInMemoryNeverOnDisk) {
  Profile* otr = profile_.GetPrimaryOTRProfile(/*create_if_needed=*/true);
  SettledVisitJournalService* service =
      SettledVisitJournalFactory::GetForProfile(otr);
  service->RecordVisit("u", GURL("https://incognito.test/x"));

  // Drain the store sequence: the visit is retained in memory ...
  std::vector<VisitRow> rows = GetVisitsSync(service);
  ASSERT_EQ(1u, rows.size());
  EXPECT_EQ("https://incognito.test/x", rows[0].url);

  // ... but nothing is ever written to disk.
  EXPECT_FALSE(base::PathExists(JournalPath(otr)));
  EXPECT_FALSE(base::PathExists(JournalPath(&profile_)));
}

TEST_F(SettledVisitJournalServiceTest, RegularJournalPersistsAcrossReopen) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const base::FilePath path =
      dir.GetPath().Append(FILE_PATH_LITERAL("RoamuxTabVisits"));
  {
    SettledVisitJournalService service(/*in_memory=*/false, path);
    service.RecordVisit("u", GURL("https://persist.test/y"));
    ASSERT_EQ(1u, GetVisitsSync(&service).size());  // barrier: commit lands
    service.Shutdown();
  }
  // A fresh service on the SAME path reads the persisted row back.
  SettledVisitJournalService reopened(/*in_memory=*/false, path);
  std::vector<VisitRow> rows = GetVisitsSync(&reopened);
  ASSERT_EQ(1u, rows.size());
  EXPECT_EQ("https://persist.test/y", rows[0].url);
  reopened.Shutdown();
}

TEST_F(SettledVisitJournalServiceTest,
       TabStatePersistsClosedAndUrlAcrossReopen) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const base::FilePath path =
      dir.GetPath().Append(FILE_PATH_LITERAL("RoamuxTabVisits"));
  {
    SettledVisitJournalService service(/*in_memory=*/false, path);
    service.SetTabState({"k", /*closed=*/true, /*window_id=*/4, "https://x/"});
    GetTabStateSync(&service, "k");  // barrier: the upsert lands
    service.Shutdown();
  }
  // A fresh service on the SAME path reads the persisted subset back, with no
  // live-session id (volatile state does not survive a new process).
  SettledVisitJournalService reopened(/*in_memory=*/false, path);
  auto [persisted, live] = GetTabStateSync(&reopened, "k");
  ASSERT_TRUE(persisted);
  EXPECT_TRUE(persisted->closed);
  EXPECT_EQ("https://x/", persisted->last_known_url);
  EXPECT_FALSE(live.has_value());
  reopened.Shutdown();
}

TEST_F(SettledVisitJournalServiceTest, LiveSessionIdIsVolatileNeverPersisted) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const base::FilePath path =
      dir.GetPath().Append(FILE_PATH_LITERAL("RoamuxTabVisits"));
  {
    SettledVisitJournalService service(/*in_memory=*/false, path);
    service.SetTabState({"k", /*closed=*/false, /*window_id=*/1, "https://x/"});
    service.SetLiveSessionId("k", "sid-123");
    auto [persisted, live] = GetTabStateSync(&service, "k");
    ASSERT_TRUE(persisted);
    ASSERT_TRUE(live.has_value());
    EXPECT_EQ("sid-123", *live);  // volatile id available in this session
    service.Shutdown();
  }
  // A fresh service: the persisted subset is there, the volatile id is gone.
  SettledVisitJournalService reopened(/*in_memory=*/false, path);
  auto [persisted, live] = GetTabStateSync(&reopened, "k");
  ASSERT_TRUE(persisted);
  EXPECT_EQ("https://x/", persisted->last_known_url);
  EXPECT_FALSE(live.has_value());  // liveSessionId was never persisted
  reopened.Shutdown();
}

class SettledVisitJournalServiceDisabledTest
    : public SettledVisitJournalServiceTestBase {
 public:
  SettledVisitJournalServiceDisabledTest() {
    features_.InitAndDisableFeature(features::kTabVisitNav);
  }

 private:
  base::test::ScopedFeatureList features_;
};

TEST_F(SettledVisitJournalServiceDisabledTest, FeatureDisabledCreatesNoDb) {
  SettledVisitJournalService* service =
      SettledVisitJournalFactory::GetForProfile(&profile_);
  ASSERT_TRUE(service);
  service->RecordVisit("u", GURL("https://disabled.test/z"));
  // Inert: no visits, no journal file.
  EXPECT_TRUE(GetVisitsSync(service).empty());
  EXPECT_FALSE(base::PathExists(JournalPath(&profile_)));
}

TEST_F(SettledVisitJournalServiceDisabledTest, FeatureDisabledNoTabState) {
  SettledVisitJournalService* service =
      SettledVisitJournalFactory::GetForProfile(&profile_);
  ASSERT_TRUE(service);
  service->SetTabState({"k", true, 1, "https://x/"});
  service->SetLiveSessionId("k", "sid");
  auto [persisted, live] = GetTabStateSync(service, "k");
  EXPECT_FALSE(persisted.has_value());
  EXPECT_FALSE(live.has_value());
  EXPECT_FALSE(base::PathExists(JournalPath(&profile_)));
}

}  // namespace
}  // namespace roamux::tab_visit

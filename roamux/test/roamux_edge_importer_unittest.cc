// SPDX-License-Identifier: Apache-2.0
// roam-15 (I-3.1): the RoamuxEdgeImporter StartImport→bridge envelope, the
// CreateImporterByType mapping, and the SourceProfile IPC ParamTraits
// round-trip (guards the raised ImporterType range). Lives in
// chrome/utility:unit_tests, which links :utility (Importer + the glue).

#include "roamux/utility/importer/roamux_edge_importer.h"

#include "base/files/scoped_temp_dir.h"
#include "base/memory/scoped_refptr.h"
#include "base/pickle.h"
#include "chrome/common/importer/importer_bridge.h"
#include "chrome/common/importer/mock_importer_bridge.h"
#include "chrome/common/importer/profile_import_process_param_traits_macros.h"
#include "chrome/utility/importer/importer.h"
#include "chrome/utility/importer/importer_creator.h"
#include "components/user_data_importer/common/importer_data_types.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::_;

namespace {

constexpr sql::Database::Tag kTag{"RoamuxEdgeImporter"};

void WriteHistoryFixture(const base::FilePath& dir) {
  sql::Database db(kTag);
  ASSERT_TRUE(db.Open(dir.Append(FILE_PATH_LITERAL("History"))));
  ASSERT_TRUE(db.Execute(
      "CREATE TABLE urls(id INTEGER PRIMARY KEY, url LONGVARCHAR, title "
      "LONGVARCHAR, visit_count INTEGER, typed_count INTEGER, "
      "last_visit_time INTEGER, hidden INTEGER DEFAULT 0)"));
  ASSERT_TRUE(db.Execute(
      "INSERT INTO urls(url, title, visit_count, typed_count, "
      "last_visit_time, hidden) VALUES('https://a.test/','A',1,0,100,0)"));
}

}  // namespace

TEST(RoamuxEdgeImporterTest, StartImportDeliversRequestedItemsOnly) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  WriteHistoryFixture(dir.GetPath());

  user_data_importer::SourceProfile profile;
  profile.importer_type = user_data_importer::TYPE_EDGE_CHROMIUM;
  profile.source_path = dir.GetPath();

  auto bridge = base::MakeRefCounted<MockImporterBridge>();
  auto importer = base::MakeRefCounted<roamux::RoamuxEdgeImporter>();

  ::testing::InSequence seq;
  EXPECT_CALL(*bridge, NotifyStarted());
  EXPECT_CALL(*bridge, NotifyItemStarted(user_data_importer::HISTORY));
  EXPECT_CALL(*bridge, SetHistoryItems(
                           _, user_data_importer::VISIT_SOURCE_EDGE_IMPORTED));
  EXPECT_CALL(*bridge, NotifyItemEnded(user_data_importer::HISTORY));
  EXPECT_CALL(*bridge, NotifyEnded());
  // Only HISTORY requested → bookmarks/keywords/autofill never delivered.
  EXPECT_CALL(*bridge, AddBookmarks(_, _)).Times(0);
  EXPECT_CALL(*bridge, SetKeywords(_, _)).Times(0);
  EXPECT_CALL(*bridge, SetAutofillFormData(_)).Times(0);

  importer->StartImport(profile, user_data_importer::HISTORY, bridge.get());
}

TEST(RoamuxEdgeImporterTest, CreateImporterByTypeReturnsRoamuxEdge) {
  scoped_refptr<Importer> importer =
      importer::CreateImporterByType(user_data_importer::TYPE_EDGE_CHROMIUM);
  ASSERT_TRUE(importer);
  EXPECT_TRUE(static_cast<bool>(
      static_cast<roamux::RoamuxEdgeImporter*>(importer.get())));
}

TEST(RoamuxEdgeImporterTest, SourceProfileTypeSurvivesIpcParamTraits) {
  user_data_importer::SourceProfile in;
  in.importer_type = user_data_importer::TYPE_EDGE_CHROMIUM;
  in.services_supported = user_data_importer::HISTORY;

  base::Pickle pickle;
  IPC::WriteParam(&pickle, in);
  base::PickleIterator iter(pickle);
  user_data_importer::SourceProfile out;
  ASSERT_TRUE(IPC::ReadParam(&pickle, &iter, &out));
  EXPECT_EQ(user_data_importer::TYPE_EDGE_CHROMIUM, out.importer_type);
  EXPECT_EQ(in.services_supported, out.services_supported);
}

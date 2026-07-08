// SPDX-License-Identifier: Apache-2.0
// roam-15 (I-3.1, review finding 2): the reader's bookmark path convention is
// correct end-to-end — feeding EdgeProfileReader output through the real
// ProfileWriter::AddBookmarks into a BookmarkModel reproduces the Edge tree
// (the toolbar sentinel makes ProfileWriter keep the first real folder), and
// empty folders survive.

#include <memory>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/importer/profile_writer.h"
#include "chrome/test/base/testing_profile.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/bookmarks/test/bookmark_test_helpers.h"
#include "content/public/test/browser_task_environment.h"
#include "roamex/utility/importer/edge_profile_reader.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace roamex {
namespace {

using bookmarks::BookmarkModel;
using bookmarks::BookmarkNode;

class RoamexEdgeBookmarksIntegrationTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(profile_dir_.CreateUniqueTempDir());
    TestingProfile::Builder builder;
    builder.AddTestingFactory(BookmarkModelFactory::GetInstance(),
                              BookmarkModelFactory::GetDefaultFactory());
    profile_ = builder.Build();
  }

  base::FilePath WriteBookmarks(const std::string& json) {
    base::WriteFile(
        profile_dir_.GetPath().Append(FILE_PATH_LITERAL("Bookmarks")), json);
    return profile_dir_.GetPath();
  }

  content::BrowserTaskEnvironment task_environment_;
  base::ScopedTempDir profile_dir_;
  std::unique_ptr<TestingProfile> profile_;
};

TEST_F(RoamexEdgeBookmarksIntegrationTest, ToolbarTreeSurvivesProfileWriter) {
  const std::string json = R"({"roots":{"bookmark_bar":{"type":"folder",
    "name":"Bookmarks bar","children":[{"type":"folder","name":"Work",
    "children":[{"type":"url","name":"Wiki","url":"https://wiki.test/",
    "date_added":"13300000000000000"},{"type":"folder","name":"EmptySub",
    "children":[]}]}]},"other":{"type":"folder","name":"Other",
    "children":[]},"synced":{"type":"folder","name":"Mobile",
    "children":[]}}})";
  const base::FilePath dir = WriteBookmarks(json);

  auto entries = EdgeProfileReader(dir).ReadBookmarks();
  ASSERT_FALSE(entries.empty());

  BookmarkModel* model =
      BookmarkModelFactory::GetForBrowserContext(profile_.get());
  bookmarks::test::WaitForBookmarkModelToLoad(model);

  auto writer = base::MakeRefCounted<ProfileWriter>(profile_.get());
  writer->AddBookmarks(entries, u"Imported from Microsoft Edge");

  // The bar has exactly the "Work" folder (the sentinel was consumed by
  // ProfileWriter, NOT flattened away).
  const BookmarkNode* bar = model->bookmark_bar_node();
  ASSERT_EQ(1u, bar->children().size());
  const BookmarkNode* work = bar->children()[0].get();
  EXPECT_TRUE(work->is_folder());
  EXPECT_EQ(u"Work", work->GetTitle());

  // Work contains Wiki (url) and the preserved empty folder.
  bool found_wiki = false, found_empty = false;
  for (const auto& child : work->children()) {
    if (!child->is_folder() && child->GetTitle() == u"Wiki") {
      found_wiki = true;
      EXPECT_EQ("https://wiki.test/", child->url().spec());
    }
    if (child->is_folder() && child->GetTitle() == u"EmptySub") {
      found_empty = true;
    }
  }
  EXPECT_TRUE(found_wiki);
  EXPECT_TRUE(found_empty) << "empty Edge folders must survive import";
}

}  // namespace
}  // namespace roamex

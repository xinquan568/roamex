// SPDX-License-Identifier: Apache-2.0
// roam-19 (I-3.5): the versioned adapter — Last-Version detection, milestone
// support gating, and per-carrier source availability (the schema-mismatch
// signal).

#include "roamux/browser/importer/edge_import_adapter.h"

#include <string>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace roamux {
namespace {

class EdgeImportAdapterTest : public testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(root_.CreateUniqueTempDir()); }

  base::FilePath UserDataDir() const {
    return root_.GetPath().Append(FILE_PATH_LITERAL("Microsoft Edge"));
  }
  base::FilePath ProfileDir() const {
    return UserDataDir().Append(FILE_PATH_LITERAL("Default"));
  }
  void WriteLastVersion(const std::string& contents) {
    ASSERT_TRUE(base::CreateDirectory(UserDataDir()));
    ASSERT_TRUE(base::WriteFile(
        UserDataDir().Append(FILE_PATH_LITERAL("Last Version")), contents));
  }

  base::ScopedTempDir root_;
};

TEST_F(EdgeImportAdapterTest, ReadsSupported150VersionAndPaths) {
  WriteLastVersion("150.0.3478.97\n");
  auto adapter = EdgeImportAdapter::Detect(root_.GetPath());
  ASSERT_TRUE(adapter->version().has_value());
  EXPECT_EQ("150.0.3478.97", adapter->version()->GetString());
  EXPECT_TRUE(adapter->version_supported());
  EXPECT_EQ(UserDataDir(), adapter->user_data_dir());
  EXPECT_EQ(ProfileDir(), adapter->profile_dir());
}

TEST_F(EdgeImportAdapterTest, MissingLastVersionIsUndeterminedFallback) {
  auto adapter = EdgeImportAdapter::Detect(root_.GetPath());
  EXPECT_FALSE(adapter->version().has_value());
  EXPECT_FALSE(adapter->version_supported());
  // Still returns a usable adapter with resolved paths (best-effort fallback).
  EXPECT_EQ(ProfileDir(), adapter->profile_dir());
}

TEST_F(EdgeImportAdapterTest, UnparseableVersionIsUndetermined) {
  WriteLastVersion("not-a-version");
  auto adapter = EdgeImportAdapter::Detect(root_.GetPath());
  EXPECT_FALSE(adapter->version().has_value());
  EXPECT_FALSE(adapter->version_supported());
}

TEST_F(EdgeImportAdapterTest, OtherMilestoneDetectedButUnsupported) {
  WriteLastVersion("152.0.1.2");
  auto adapter = EdgeImportAdapter::Detect(root_.GetPath());
  ASSERT_TRUE(adapter->version().has_value());
  EXPECT_FALSE(adapter->version_supported());
}

TEST_F(EdgeImportAdapterTest, CarrierAvailableReflectsSourceShape) {
  WriteLastVersion("150.0.0.0");
  ASSERT_TRUE(base::CreateDirectory(ProfileDir()));

  auto absent = EdgeImportAdapter::Detect(root_.GetPath());
  EXPECT_FALSE(absent->CarrierAvailable(EdgeCarrier::kPasswords));
  EXPECT_FALSE(absent->CarrierAvailable(EdgeCarrier::kCookies));
  EXPECT_FALSE(absent->CarrierAvailable(EdgeCarrier::kLocalStorage));
  EXPECT_FALSE(absent->CarrierAvailable(EdgeCarrier::kIndexedDb));

  ASSERT_TRUE(base::WriteFile(
      ProfileDir().Append(FILE_PATH_LITERAL("Login Data")), "x"));
  ASSERT_TRUE(
      base::WriteFile(ProfileDir().Append(FILE_PATH_LITERAL("Cookies")), "x"));
  ASSERT_TRUE(
      base::CreateDirectory(ProfileDir()
                                .Append(FILE_PATH_LITERAL("Local Storage"))
                                .Append(FILE_PATH_LITERAL("leveldb"))));
  ASSERT_TRUE(base::CreateDirectory(
      ProfileDir().Append(FILE_PATH_LITERAL("IndexedDB"))));

  auto present = EdgeImportAdapter::Detect(root_.GetPath());
  EXPECT_TRUE(present->CarrierAvailable(EdgeCarrier::kPasswords));
  EXPECT_TRUE(present->CarrierAvailable(EdgeCarrier::kCookies));
  EXPECT_TRUE(present->CarrierAvailable(EdgeCarrier::kLocalStorage));
  EXPECT_TRUE(present->CarrierAvailable(EdgeCarrier::kIndexedDb));
}

}  // namespace
}  // namespace roamux

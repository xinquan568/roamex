// SPDX-License-Identifier: Apache-2.0
#include "roamex/browser/importer/edge_import_adapter.h"

#include <string>
#include <string_view>

#include "base/files/file_util.h"
#include "base/memory/ptr_util.h"
#include "base/strings/string_util.h"

namespace roamex {

namespace {

// The Edge macOS User-Data root under the Application-Support directory.
constexpr base::FilePath::CharType kEdgeUserData[] =
    FILE_PATH_LITERAL("Microsoft Edge");
constexpr base::FilePath::CharType kDefaultProfile[] =
    FILE_PATH_LITERAL("Default");
constexpr base::FilePath::CharType kLastVersion[] =
    FILE_PATH_LITERAL("Last Version");

}  // namespace

std::optional<base::Version> DetectEdgeVersion(
    const base::FilePath& user_data_dir) {
  std::string contents;
  if (!base::ReadFileToString(user_data_dir.Append(kLastVersion), &contents)) {
    return std::nullopt;
  }
  const std::string_view trimmed =
      base::TrimWhitespaceASCII(contents, base::TRIM_ALL);
  base::Version version(trimmed);
  if (!version.IsValid()) {
    return std::nullopt;
  }
  return version;
}

// static
std::unique_ptr<EdgeImportAdapter> EdgeImportAdapter::Detect(
    const base::FilePath& app_data_root) {
  base::FilePath user_data_dir = app_data_root.Append(kEdgeUserData);
  base::FilePath profile_dir = user_data_dir.Append(kDefaultProfile);
  std::optional<base::Version> version = DetectEdgeVersion(user_data_dir);
  const bool supported = version.has_value() &&
                         !version->components().empty() &&
                         version->components()[0] == kSupportedMilestone;
  return base::WrapUnique(new EdgeImportAdapter(
      std::move(user_data_dir), std::move(profile_dir), version, supported));
}

EdgeImportAdapter::EdgeImportAdapter(base::FilePath user_data_dir,
                                     base::FilePath profile_dir,
                                     std::optional<base::Version> version,
                                     bool version_supported)
    : user_data_dir_(std::move(user_data_dir)),
      profile_dir_(std::move(profile_dir)),
      version_(std::move(version)),
      version_supported_(version_supported) {}

EdgeImportAdapter::~EdgeImportAdapter() = default;

bool EdgeImportAdapter::CarrierAvailable(EdgeCarrier carrier) const {
  switch (carrier) {
    case EdgeCarrier::kPasswords:
      return base::PathExists(
          profile_dir_.Append(FILE_PATH_LITERAL("Login Data")));
    case EdgeCarrier::kCookies:
      return base::PathExists(
          profile_dir_.Append(FILE_PATH_LITERAL("Cookies")));
    case EdgeCarrier::kLocalStorage:
      return base::DirectoryExists(
          profile_dir_.Append(FILE_PATH_LITERAL("Local Storage"))
              .Append(FILE_PATH_LITERAL("leveldb")));
    case EdgeCarrier::kIndexedDb:
      return base::DirectoryExists(
          profile_dir_.Append(FILE_PATH_LITERAL("IndexedDB")));
  }
}

}  // namespace roamex

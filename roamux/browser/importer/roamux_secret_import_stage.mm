// SPDX-License-Identifier: Apache-2.0
#include "roamux/browser/importer/roamux_secret_import_stage.h"

#include <string>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/importer/profile_writer.h"
#include "components/password_manager/core/browser/password_form.h"
#include "crypto/hash.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_constants.h"
#include "net/cookies/cookie_partition_key.h"
#include "roamux/browser/importer/edge_secret_decryptor.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "url/gurl.h"

namespace roamux {

namespace {
constexpr sql::Database::Tag kTag{"RoamuxEdgeImporter"};
}  // namespace

RoamuxSecretImportStage::RoamuxSecretImportStage(
    base::FilePath source_path,
    scoped_refptr<ProfileWriter> writer,
    crypto::apple::KeychainV2* keychain_for_testing)
    : source_path_(std::move(source_path)),
      writer_(std::move(writer)),
      keychain_for_testing_(keychain_for_testing) {}

RoamuxSecretImportStage::~RoamuxSecretImportStage() = default;

size_t RoamuxSecretImportStage::ImportPasswords() {
  EdgeSecretDecryptor decryptor(keychain_for_testing_);
  keychain_available_ = decryptor.status() == EdgeSecretDecryptor::Status::kOk;
  if (!keychain_available_) {
    return 0;  // Declined/unavailable — reported via keychain_available().
  }

  const base::FilePath source =
      source_path_.Append(FILE_PATH_LITERAL("Login Data"));
  if (!base::PathExists(source)) {
    return 0;
  }
  // Copy-to-temp + read-only (roam-15 discipline): never touch the live DB.
  base::ScopedTempDir temp_dir;
  if (!temp_dir.CreateUniqueTempDir()) {
    return 0;
  }
  const base::FilePath copy =
      temp_dir.GetPath().Append(FILE_PATH_LITERAL("Login Data"));
  if (!base::CopyFile(source, copy)) {
    return 0;
  }
  sql::Database db(kTag);
  if (!db.Open(copy)) {
    return 0;
  }
  sql::Statement s(db.GetUniqueStatement(
      "SELECT origin_url, action_url, username_element, username_value, "
      "password_element, password_value, signon_realm, scheme, "
      "blacklisted_by_user FROM logins"));
  if (!s.is_valid()) {
    return 0;
  }
  size_t imported = 0;
  while (s.Step()) {
    password_manager::PasswordForm form;
    form.url = GURL(s.ColumnStringView(0));
    form.action = GURL(s.ColumnStringView(1));
    form.username_element = s.ColumnString16(2);
    form.password_element = s.ColumnString16(4);
    form.signon_realm = s.ColumnString(6);
    form.scheme =
        static_cast<password_manager::PasswordForm::Scheme>(s.ColumnInt(7));
    const bool blocked = s.ColumnInt(8) != 0;
    if (!form.url.is_valid() || form.signon_realm.empty()) {
      continue;
    }
    if (blocked) {
      // Declined-vs-empty: a "never save" entry has no ciphertext; import it as
      // a blocked form rather than skipping it.
      form.blocked_by_user = true;
    } else {
      std::string blob = s.ColumnString(5);
      std::optional<std::string> password =
          decryptor.DecryptV10(base::as_byte_span(blob));
      if (!password) {
        continue;  // Unparseable saved row — skip, don't abort the batch.
      }
      form.username_value = s.ColumnString16(3);
      form.password_value = base::UTF8ToUTF16(*password);
    }
    writer_->AddPasswordForm(form);  // Re-encrypts under Roamux on store.
    ++imported;
  }
  return imported;
}

std::vector<net::CanonicalCookie> RoamuxSecretImportStage::ParseCookies() {
  EdgeSecretDecryptor decryptor(keychain_for_testing_);
  keychain_available_ = decryptor.status() == EdgeSecretDecryptor::Status::kOk;
  std::vector<net::CanonicalCookie> cookies;
  if (!keychain_available_) {
    return cookies;
  }
  const base::FilePath source =
      source_path_.Append(FILE_PATH_LITERAL("Cookies"));
  base::ScopedTempDir temp_dir;
  if (base::PathExists(source) && temp_dir.CreateUniqueTempDir()) {
    const base::FilePath copy =
        temp_dir.GetPath().Append(FILE_PATH_LITERAL("Cookies"));
    sql::Database db(kTag);
    if (base::CopyFile(source, copy) && db.Open(copy)) {
      sql::Statement s(db.GetUniqueStatement(
          "SELECT host_key, top_frame_site_key, name, encrypted_value, path, "
          "creation_utc, expires_utc, last_access_utc, last_update_utc, "
          "is_secure, is_httponly, samesite, priority, source_scheme, "
          "source_port, source_type, has_cross_site_ancestor FROM cookies"));
      if (s.is_valid()) {
        while (s.Step()) {
          const std::string host_key = s.ColumnString(0);
          const std::string top_frame_site_key = s.ColumnString(1);
          // First-party / unpartitioned only: partitioned cookies carry a
          // non-empty top_frame_site_key (has_cross_site_ancestor alone is set
          // even for unpartitioned cookies, so it is NOT the filter).
          if (!top_frame_site_key.empty()) {
            continue;
          }
          std::string blob = s.ColumnString(3);
          std::optional<std::string> plain =
              decryptor.DecryptV10(base::as_byte_span(blob));
          if (!plain) {
            continue;
          }
          // v24 payload = SHA256(host_key) || value; verify + strip the prefix.
          const std::array<uint8_t, 32> expected =
              crypto::hash::Sha256(host_key);
          if (plain->size() < expected.size() ||
              base::as_byte_span(*plain).first(expected.size()) !=
                  base::span(expected)) {
            continue;  // Prefix mismatch → drop (never import a corrupt value).
          }
          const std::string value = plain->substr(expected.size());
          std::unique_ptr<net::CanonicalCookie> cookie =
              net::CanonicalCookie::FromStorage(
                  s.ColumnString(2), value, host_key, s.ColumnString(4),
                  base::Time::FromDeltaSinceWindowsEpoch(
                      base::Microseconds(s.ColumnInt64(5))),
                  base::Time::FromDeltaSinceWindowsEpoch(
                      base::Microseconds(s.ColumnInt64(6))),
                  base::Time::FromDeltaSinceWindowsEpoch(
                      base::Microseconds(s.ColumnInt64(7))),
                  base::Time::FromDeltaSinceWindowsEpoch(
                      base::Microseconds(s.ColumnInt64(8))),
                  s.ColumnBool(9), s.ColumnBool(10),
                  static_cast<net::CookieSameSite>(s.ColumnInt(11)),
                  static_cast<net::CookiePriority>(s.ColumnInt(12)),
                  /*partition_key=*/std::nullopt,
                  static_cast<net::CookieSourceScheme>(s.ColumnInt(13)),
                  s.ColumnInt(14),
                  static_cast<net::CookieSourceType>(s.ColumnInt(15)),
                  net::CanonicalCookieFromStorageCallSite::kCookieManager);
          if (cookie) {
            cookies.push_back(std::move(*cookie));
          }
        }
      }
    }
  }
  return cookies;
}

void RoamuxSecretImportStage::ImportCookies(
    base::OnceCallback<void(size_t)> done) {
  std::vector<net::CanonicalCookie> cookies = ParseCookies();
  // AddCookies reports the count the destination store ACCEPTED (IsInclude).
  writer_->AddCookies(cookies, std::move(done));
}

std::vector<net::CanonicalCookie>
RoamuxSecretImportStage::ParseCookiesForTesting() {
  return ParseCookies();
}

void RoamuxSecretImportStage::Run(uint16_t items,
                                  base::OnceCallback<void(Result)> done) {
  // The browser-side secret half of an Edge import: invoked by the import flow
  // (roam-20's first-run UI) with the user-selected item mask, in parallel with
  // the utility-process non-secret importer. PASSWORDS/COOKIES are handled ONLY
  // here (the utility importer never touches secrets).
  auto result = std::make_unique<Result>();
  if (items & user_data_importer::PASSWORDS) {
    result->passwords_imported = ImportPasswords();
  }
  result->keychain_available = keychain_available_;
  if (items & user_data_importer::COOKIES) {
    ImportCookies(base::BindOnce(
        [](std::unique_ptr<Result> result,
           base::OnceCallback<void(Result)> done, size_t cookies) {
          result->cookies_imported = cookies;
          std::move(done).Run(*result);
        },
        std::move(result), std::move(done)));
    return;
  }
  std::move(done).Run(*result);
}

}  // namespace roamux

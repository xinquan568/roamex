// SPDX-License-Identifier: Apache-2.0
// roam-16 (I-3.2): the browser-side secret stage — Edge Login Data / Cookies
// decrypt → ProfileWriter → destination store, proving the v24 SHA256(host_key)
// strip and first-party filter end to end, plus the declined-Keychain report.

#include "roamux/browser/importer/roamux_secret_import_stage.h"

#include <array>

#include "base/files/scoped_temp_dir.h"
#include "base/memory/scoped_refptr.h"
#include "base/test/test_future.h"
#include "chrome/browser/importer/profile_writer.h"
#include "chrome/browser/password_manager/password_manager_test_util.h"
#include "chrome/test/base/testing_profile.h"
#include "components/password_manager/core/browser/password_store/test_password_store.h"
#include "components/user_data_importer/common/importer_data_types.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/test/browser_task_environment.h"
#include "crypto/aes_cbc.h"
#include "crypto/apple/keychain_v2.h"
#include "crypto/hash.h"
#include "crypto/kdf.h"
#include "net/cookies/cookie_access_result.h"
#include "roamux/common/roamux_crypto_passkey.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace roamux {
namespace {

constexpr char kEdgePassword[] = "edge-storage-pw";
constexpr sql::Database::Tag kTag{"RoamuxEdgeImporter"};

class EdgeKeychainFake : public crypto::apple::KeychainV2 {
 public:
  explicit EdgeKeychainFake(OSStatus status) : status_(status) {}
  base::expected<std::vector<uint8_t>, OSStatus> FindGenericPassword(
      std::string_view,
      std::string_view) override {
    if (status_ != noErr) {
      return base::unexpected(status_);
    }
    const std::string pw(kEdgePassword);
    return std::vector<uint8_t>(pw.begin(), pw.end());
  }

 private:
  OSStatus status_;
};

std::vector<uint8_t> EdgeEncrypt(const std::string& plaintext) {
  std::array<uint8_t, 16> key;
  const std::array<uint8_t, 9> salt = {'s', 'a', 'l', 't', 'y',
                                       's', 'a', 'l', 't'};
  crypto::kdf::Pbkdf2HmacSha1({.iterations = 1003},
                              base::as_byte_span(std::string(kEdgePassword)),
                              salt, key, MakeCryptoPassKey());
  const std::array<uint8_t, 16> iv = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
                                      ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
  std::vector<uint8_t> ct =
      crypto::aes_cbc::Encrypt(key, iv, base::as_byte_span(plaintext));
  std::vector<uint8_t> blob = {'v', '1', '0'};
  blob.insert(blob.end(), ct.begin(), ct.end());
  return blob;
}

class RoamuxSecretImportStageTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(dir_.CreateUniqueTempDir());
    profile_ = TestingProfile::Builder().Build();
    CreateAndUseTestPasswordStore(profile_.get());
  }

  base::FilePath source() { return dir_.GetPath(); }
  scoped_refptr<ProfileWriter> writer() {
    return base::MakeRefCounted<ProfileWriter>(profile_.get());
  }

  content::BrowserTaskEnvironment task_environment_;
  base::ScopedTempDir dir_;
  std::unique_ptr<TestingProfile> profile_;
};

TEST_F(RoamuxSecretImportStageTest, ImportsDecryptedPasswords) {
  {
    sql::Database db(kTag);
    ASSERT_TRUE(db.Open(source().Append(FILE_PATH_LITERAL("Login Data"))));
    ASSERT_TRUE(db.Execute(
        "CREATE TABLE logins(origin_url VARCHAR, action_url VARCHAR, "
        "username_element VARCHAR, username_value VARCHAR, password_element "
        "VARCHAR, password_value BLOB, signon_realm VARCHAR, scheme INTEGER, "
        "blacklisted_by_user INTEGER)"));
    std::vector<uint8_t> blob = EdgeEncrypt("s3cret");
    sql::Statement stmt(db.GetUniqueStatement(
        "INSERT INTO logins VALUES('https://site.test/','','','alice','',?,"
        "'https://site.test/',0,0)"));
    stmt.BindBlob(0, blob);
    ASSERT_TRUE(stmt.Run());
  }

  EdgeKeychainFake keychain(noErr);
  RoamuxSecretImportStage stage(source(), writer(), &keychain);
  EXPECT_EQ(1u, stage.ImportPasswords());
  EXPECT_TRUE(stage.keychain_available());
}

TEST_F(RoamuxSecretImportStageTest, DeclinedKeychainImportsNothingReported) {
  EdgeKeychainFake keychain(errSecAuthFailed);
  RoamuxSecretImportStage stage(source(), writer(), &keychain);
  EXPECT_EQ(0u, stage.ImportPasswords());
  EXPECT_FALSE(stage.keychain_available());
}

TEST_F(RoamuxSecretImportStageTest, ImportsFirstPartyCookieWithV24Strip) {
  {
    sql::Database db(kTag);
    ASSERT_TRUE(db.Open(source().Append(FILE_PATH_LITERAL("Cookies"))));
    ASSERT_TRUE(db.Execute(
        "CREATE TABLE cookies(host_key TEXT, top_frame_site_key TEXT, name "
        "TEXT, encrypted_value BLOB, path TEXT, creation_utc INTEGER, "
        "expires_utc INTEGER, last_access_utc INTEGER, last_update_utc "
        "INTEGER, is_secure INTEGER, is_httponly INTEGER, samesite INTEGER, "
        "priority INTEGER, source_scheme INTEGER, source_port INTEGER, "
        "source_type INTEGER, has_cross_site_ancestor INTEGER)"));
    const std::string host = "site.test";
    const std::array<uint8_t, 32> prefix = crypto::hash::Sha256(host);
    std::string payload(prefix.begin(), prefix.end());
    payload += "cookievalue";  // v24: SHA256(host_key) || value.
    std::vector<uint8_t> enc = EdgeEncrypt(payload);
    sql::Statement stmt(db.GetUniqueStatement(
        "INSERT INTO cookies VALUES('site.test','','sid',?,'/',"
        "13300000000000000,13400000000000000,13300000000000000,"
        "13300000000000000,1,0,0,1,2,443,0,1)"));
    stmt.BindBlob(0, enc);
    ASSERT_TRUE(stmt.Run());
  }

  EdgeKeychainFake keychain(noErr);
  RoamuxSecretImportStage stage(source(), writer(), &keychain);
  // The v24 SHA256(host_key) prefix is verified + stripped, leaving the value.
  std::vector<net::CanonicalCookie> cookies = stage.ParseCookiesForTesting();
  ASSERT_EQ(1u, cookies.size());
  EXPECT_EQ("sid", cookies[0].Name());
  EXPECT_EQ("cookievalue", cookies[0].Value());
  EXPECT_EQ("site.test", cookies[0].Domain());
  EXPECT_TRUE(stage.keychain_available());
}

TEST_F(RoamuxSecretImportStageTest, PartitionedCookieSkipped) {
  {
    sql::Database db(kTag);
    ASSERT_TRUE(db.Open(source().Append(FILE_PATH_LITERAL("Cookies"))));
    ASSERT_TRUE(db.Execute(
        "CREATE TABLE cookies(host_key TEXT, top_frame_site_key TEXT, name "
        "TEXT, encrypted_value BLOB, path TEXT, creation_utc INTEGER, "
        "expires_utc INTEGER, last_access_utc INTEGER, last_update_utc "
        "INTEGER, is_secure INTEGER, is_httponly INTEGER, samesite INTEGER, "
        "priority INTEGER, source_scheme INTEGER, source_port INTEGER, "
        "source_type INTEGER, has_cross_site_ancestor INTEGER)"));
    // Partitioned: non-empty top_frame_site_key → skipped.
    ASSERT_TRUE(
        db.Execute("INSERT INTO cookies VALUES('site.test','https://top.test',"
                   "'sid',X'7631300102','/',0,0,0,0,1,0,0,1,2,443,0,1)"));
  }

  EdgeKeychainFake keychain(noErr);
  RoamuxSecretImportStage stage(source(), writer(), &keychain);
  // Partitioned (non-empty top_frame_site_key) → filtered out.
  EXPECT_TRUE(stage.ParseCookiesForTesting().empty());
}

TEST_F(RoamuxSecretImportStageTest, BlockedPasswordImportedNotSkipped) {
  {
    sql::Database db(kTag);
    ASSERT_TRUE(db.Open(source().Append(FILE_PATH_LITERAL("Login Data"))));
    ASSERT_TRUE(db.Execute(
        "CREATE TABLE logins(origin_url VARCHAR, action_url VARCHAR, "
        "username_element VARCHAR, username_value VARCHAR, password_element "
        "VARCHAR, password_value BLOB, signon_realm VARCHAR, scheme INTEGER, "
        "blacklisted_by_user INTEGER)"));
    // A "never save" row: empty password blob, blacklisted_by_user=1.
    ASSERT_TRUE(db.Execute(
        "INSERT INTO logins VALUES('https://blocked.test/','','','','',X'',"
        "'https://blocked.test/',0,1)"));
  }
  EdgeKeychainFake keychain(noErr);
  RoamuxSecretImportStage stage(source(), writer(), &keychain);
  // Imported as a blocked form (declined-vs-empty), not silently dropped.
  EXPECT_EQ(1u, stage.ImportPasswords());
}

TEST_F(RoamuxSecretImportStageTest, RunImportsSelectedItems) {
  {
    sql::Database db(kTag);
    ASSERT_TRUE(db.Open(source().Append(FILE_PATH_LITERAL("Login Data"))));
    ASSERT_TRUE(db.Execute(
        "CREATE TABLE logins(origin_url VARCHAR, action_url VARCHAR, "
        "username_element VARCHAR, username_value VARCHAR, password_element "
        "VARCHAR, password_value BLOB, signon_realm VARCHAR, scheme INTEGER, "
        "blacklisted_by_user INTEGER)"));
    std::vector<uint8_t> blob = EdgeEncrypt("s3cret");
    sql::Statement stmt(db.GetUniqueStatement(
        "INSERT INTO logins VALUES('https://site.test/','','','alice','',?,"
        "'https://site.test/',0,0)"));
    stmt.BindBlob(0, blob);
    ASSERT_TRUE(stmt.Run());
  }
  EdgeKeychainFake keychain(noErr);
  RoamuxSecretImportStage stage(source(), writer(), &keychain);
  base::test::TestFuture<RoamuxSecretImportStage::Result> result;
  stage.Run(user_data_importer::PASSWORDS, result.GetCallback());
  EXPECT_EQ(1u, result.Get().passwords_imported);
  EXPECT_TRUE(result.Get().keychain_available);
}

}  // namespace
}  // namespace roamux

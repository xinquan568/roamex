// SPDX-License-Identifier: Apache-2.0
// roam-16 (I-3.2): the Edge secret decryptor — v10 AES-128-CBC round-trip
// keyed from the 'Microsoft Edge Safe Storage' Keychain item, and the
// declined-Keychain path reported (never a silent drop).

#include "roamux/browser/importer/edge_secret_decryptor.h"

#include <array>

#include "base/containers/span.h"
#include "crypto/aes_cbc.h"
#include "crypto/apple/keychain_v2.h"
#include "crypto/kdf.h"
#include "roamux/common/roamux_crypto_passkey.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace roamux {
namespace {

// A KeychainV2 fake that asserts the exact Edge service/account and returns a
// chosen password (or an error to simulate a declined prompt). FakeKeychainV2
// returns a fixed mock password regardless of service/account, so we need this
// to prove we query the right item.
class EdgeKeychainFake : public crypto::apple::KeychainV2 {
 public:
  EdgeKeychainFake(std::string password, OSStatus status)
      : password_(std::move(password)), status_(status) {}

  base::expected<std::vector<uint8_t>, OSStatus> FindGenericPassword(
      std::string_view service_name,
      std::string_view account_name) override {
    seen_service_ = std::string(service_name);
    seen_account_ = std::string(account_name);
    if (status_ != noErr) {
      return base::unexpected(status_);
    }
    return std::vector<uint8_t>(password_.begin(), password_.end());
  }

  const std::string& seen_service() const { return seen_service_; }
  const std::string& seen_account() const { return seen_account_; }

 private:
  std::string password_;
  OSStatus status_;
  std::string seen_service_;
  std::string seen_account_;
};

// Encrypts `plaintext` exactly as Edge would, given the Keychain password.
std::vector<uint8_t> EncryptLikeEdge(const std::string& keychain_password,
                                     const std::string& plaintext) {
  std::array<uint8_t, 16> key;
  const std::array<uint8_t, 9> salt = {'s', 'a', 'l', 't', 'y',
                                       's', 'a', 'l', 't'};
  crypto::kdf::Pbkdf2HmacSha1({.iterations = 1003},
                              base::as_byte_span(keychain_password), salt, key,
                              MakeCryptoPassKey());
  const std::array<uint8_t, 16> iv = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
                                      ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
  std::vector<uint8_t> ct =
      crypto::aes_cbc::Encrypt(key, iv, base::as_byte_span(plaintext));
  std::vector<uint8_t> blob = {'v', '1', '0'};
  blob.insert(blob.end(), ct.begin(), ct.end());
  return blob;
}

TEST(EdgeSecretDecryptorTest, RoundTripsV10AndQueriesEdgeItem) {
  EdgeKeychainFake keychain("edge-storage-pw", noErr);
  EdgeSecretDecryptor decryptor(&keychain);
  EXPECT_EQ(EdgeSecretDecryptor::Status::kOk, decryptor.status());
  EXPECT_EQ("Microsoft Edge Safe Storage", keychain.seen_service());
  EXPECT_EQ("Microsoft Edge", keychain.seen_account());

  const std::vector<uint8_t> blob =
      EncryptLikeEdge("edge-storage-pw", "hunter2");
  std::optional<std::string> plain = decryptor.DecryptV10(blob);
  ASSERT_TRUE(plain.has_value());
  EXPECT_EQ("hunter2", *plain);
}

TEST(EdgeSecretDecryptorTest, DeclinedKeychainReportedNotDropped) {
  EdgeKeychainFake keychain("", errSecAuthFailed);
  EdgeSecretDecryptor decryptor(&keychain);
  EXPECT_EQ(EdgeSecretDecryptor::Status::kKeychainUnavailable,
            decryptor.status());
  // Any decrypt attempt fails cleanly (reported via status(), not a
  // crash/drop).
  const std::array<uint8_t, 4> blob = {'v', '1', '0', 0x00};
  EXPECT_FALSE(decryptor.DecryptV10(blob).has_value());
}

TEST(EdgeSecretDecryptorTest, NonV10BlobRejected) {
  EdgeKeychainFake keychain("edge-storage-pw", noErr);
  EdgeSecretDecryptor decryptor(&keychain);
  const std::array<uint8_t, 5> blob = {'x', 'y', 'z', 0x01, 0x02};
  EXPECT_FALSE(decryptor.DecryptV10(blob).has_value());
}

}  // namespace
}  // namespace roamux

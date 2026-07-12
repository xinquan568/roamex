// SPDX-License-Identifier: Apache-2.0
#include "roamux/browser/importer/edge_secret_decryptor.h"

#include <array>

#include "base/containers/span.h"
#include "crypto/aes_cbc.h"
#include "crypto/apple/keychain_v2.h"
#include "crypto/kdf.h"
#include "roamux/common/roamux_crypto_passkey.h"

namespace roamux {

namespace {

// The Chromium macOS secret scheme (mirrors keychain_key_provider.mm).
constexpr char kEdgeService[] = "Microsoft Edge Safe Storage";
constexpr char kEdgeAccount[] = "Microsoft Edge";
constexpr uint32_t kIterations = 1003;
constexpr size_t kDerivedKeySize = 16;
constexpr std::array<uint8_t, 9> kSalt = {'s', 'a', 'l', 't', 'y',
                                          's', 'a', 'l', 't'};
constexpr char kVersionPrefix[] = "v10";
// AES-128-CBC IV is 16 space characters.
constexpr std::array<uint8_t, 16> kIv = {' ', ' ', ' ', ' ', ' ', ' ',
                                         ' ', ' ', ' ', ' ', ' ', ' ',
                                         ' ', ' ', ' ', ' '};

}  // namespace

EdgeSecretDecryptor::EdgeSecretDecryptor(
    crypto::apple::KeychainV2* keychain_for_testing) {
  crypto::apple::KeychainV2& keychain =
      keychain_for_testing ? *keychain_for_testing
                           : crypto::apple::KeychainV2::GetInstance();
  base::expected<std::vector<uint8_t>, OSStatus> password =
      keychain.FindGenericPassword(kEdgeService, kEdgeAccount);
  if (!password.has_value() || password->empty()) {
    status_ = Status::kKeychainUnavailable;  // Declined / error — reported.
    return;
  }
  key_.resize(kDerivedKeySize);
  crypto::kdf::Pbkdf2HmacSha1({.iterations = kIterations}, *password, kSalt,
                              key_, MakeCryptoPassKey());
  status_ = Status::kOk;
}

EdgeSecretDecryptor::~EdgeSecretDecryptor() {
  std::fill(key_.begin(), key_.end(), 0u);  // Best-effort zeroization.
}

std::optional<std::string> EdgeSecretDecryptor::DecryptV10(
    base::span<const uint8_t> blob) const {
  if (status_ != Status::kOk) {
    return std::nullopt;
  }
  const size_t prefix_len = std::size(kVersionPrefix) - 1;  // "v10"
  if (blob.size() < prefix_len ||
      std::string_view(reinterpret_cast<const char*>(blob.data()),
                       prefix_len) != kVersionPrefix) {
    return std::nullopt;
  }
  std::optional<std::vector<uint8_t>> plain =
      crypto::aes_cbc::Decrypt(key_, kIv, blob.subspan(prefix_len));
  if (!plain) {
    return std::nullopt;
  }
  return std::string(plain->begin(), plain->end());
}

}  // namespace roamux

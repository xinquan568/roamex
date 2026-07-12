// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_BROWSER_IMPORTER_EDGE_SECRET_DECRYPTOR_H_
#define ROAMUX_BROWSER_IMPORTER_EDGE_SECRET_DECRYPTOR_H_

#include <optional>
#include <string>
#include <vector>

#include "base/containers/span.h"

namespace crypto::apple {
class KeychainV2;
}

namespace roamux {

// Decrypts Microsoft Edge (Chromium, macOS) "v10" secret blobs — login
// passwords and cookie values — using the key derived from Edge's own Keychain
// item 'Microsoft Edge Safe Storage' (roam-16 / I-3.2, §5.2).
//
// SECURITY (frozen analysis, Step-2 finding 2): the Keychain read AND the
// decryption run in ONE process (browser-side); the raw Keychain password and
// the derived key never leave this object / never cross IPC. Only decrypted
// per-record plaintext is handed onward.
class EdgeSecretDecryptor {
 public:
  enum class Status {
    kOk,                   // Key derived; ready to decrypt.
    kKeychainUnavailable,  // Prompt declined / read error — REPORT, never drop.
  };

  // Prod: pass nullptr (uses crypto::apple::KeychainV2::GetInstance()).
  // Tests: inject a fake. Reads 'Microsoft Edge Safe Storage' / 'Microsoft
  // Edge'.
  explicit EdgeSecretDecryptor(crypto::apple::KeychainV2* keychain_for_testing);
  EdgeSecretDecryptor(const EdgeSecretDecryptor&) = delete;
  EdgeSecretDecryptor& operator=(const EdgeSecretDecryptor&) = delete;
  ~EdgeSecretDecryptor();

  Status status() const { return status_; }

  // Decrypts a "v10"-prefixed AES-128-CBC blob. Returns nullopt on a bad
  // prefix, wrong padding, or when status() != kOk.
  std::optional<std::string> DecryptV10(base::span<const uint8_t> blob) const;

 private:
  Status status_ = Status::kKeychainUnavailable;
  std::vector<uint8_t> key_;  // 16-byte AES key; zeroed on destruction.
};

}  // namespace roamux

#endif  // ROAMUX_BROWSER_IMPORTER_EDGE_SECRET_DECRYPTOR_H_

// SPDX-License-Identifier: Apache-2.0
#include "roamex/app/appcast_verifier.h"

#include <optional>
#include <vector>

#include "base/base64.h"
#include "third_party/boringssl/src/include/openssl/curve25519.h"

namespace roamex::app {

bool VerifyAppcastSignature(base::span<const uint8_t> enclosure_bytes,
                            const std::string& signature_base64,
                            const std::string& public_key_base64) {
  std::optional<std::vector<uint8_t>> signature =
      base::Base64Decode(signature_base64);
  std::optional<std::vector<uint8_t>> public_key =
      base::Base64Decode(public_key_base64);
  if (!signature || signature->size() != ED25519_SIGNATURE_LEN || !public_key ||
      public_key->size() != ED25519_PUBLIC_KEY_LEN) {
    return false;
  }
  return ED25519_verify(enclosure_bytes.data(), enclosure_bytes.size(),
                        signature->data(), public_key->data()) == 1;
}

}  // namespace roamex::app

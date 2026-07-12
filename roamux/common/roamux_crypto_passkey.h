// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_COMMON_ROAMUX_CRYPTO_PASSKEY_H_
#define ROAMUX_COMMON_ROAMUX_CRYPTO_PASSKEY_H_

#include "crypto/subtle_passkey.h"

namespace roamux {

// Mints a crypto::SubtlePassKey for Roamux's own use of low-level crypto KDFs
// (the Edge import decryptor derives the Edge key via crypto::kdf). Mirrors the
// password_manager::MakeCryptoPassKeyForPasswordHash precedent; friended in
// crypto/subtle_passkey.h by patch 0014.
crypto::SubtlePassKey MakeCryptoPassKey();

}  // namespace roamux

#endif  // ROAMUX_COMMON_ROAMUX_CRYPTO_PASSKEY_H_

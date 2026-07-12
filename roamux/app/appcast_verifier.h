// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_APP_APPCAST_VERIFIER_H_
#define ROAMUX_APP_APPCAST_VERIFIER_H_

#include <string>

#include "base/containers/span.h"

// Ed25519 verification of a Sparkle appcast enclosure (roam-32, plan
// §13.6/K3): sparkle:edSignature is Ed25519 over the enclosure bytes,
// base64-encoded, verified against the (base64) SUPublicEDKey. Used by the
// fixture tests here and by I-6.3's release-pipeline validation. Sparkle
// performs its own equivalent check in-app; this is the pipeline-side seam.
namespace roamux::app {

// True iff `signature_base64` verifies over `enclosure_bytes` with
// `public_key_base64` (both base64 per Sparkle's appcast/key format).
bool VerifyAppcastSignature(base::span<const uint8_t> enclosure_bytes,
                            const std::string& signature_base64,
                            const std::string& public_key_base64);

}  // namespace roamux::app

#endif  // ROAMUX_APP_APPCAST_VERIFIER_H_

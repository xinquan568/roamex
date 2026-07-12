# SPDX-License-Identifier: Apache-2.0
"""The roam-33 signing-mode gate (plan §13.6/K4, tier-3).

Pure decision over the Apple secret set: all present -> signed; none -> a
deliberate unsigned personal-alpha; any partial -> fail-fast naming the
missing ones (a half-signed build is worse than either honest state). The
Sparkle EdDSA signature (roam-32) is required in EVERY mode; this gate governs
only Apple's codesign/notarize layer.
"""

import os
import sys

# The enumerated Apple secret set (App Store Connect API-key notary form —
# rotatable, no 2FA). Present in the protected `release` GitHub Environment.
REQUIRED_SECRETS = (
    "ROAMEX_DEVELOPER_ID_CERT_P12",
    "ROAMEX_DEVELOPER_ID_CERT_PASSWORD",
    "ROAMEX_NOTARY_KEY_ID",
    "ROAMEX_NOTARY_ISSUER_ID",
    "ROAMEX_NOTARY_PRIVATE_KEY",
)


class PartialSigningSecretsError(RuntimeError):
    """Some but not all Apple signing secrets are present."""


def resolve_signing_mode(env):
    """'signed' if all secrets present, 'unsigned' if none, else raise."""
    present = [k for k in REQUIRED_SECRETS if env.get(k, "").strip()]
    if len(present) == len(REQUIRED_SECRETS):
        return "signed"
    if not present:
        return "unsigned"
    missing = [k for k in REQUIRED_SECRETS if not env.get(k, "").strip()]
    raise PartialSigningSecretsError(
        "partial Apple signing secrets — refusing a half-signed build. "
        "Missing: " + ", ".join(missing))


def main():
    try:
        mode = resolve_signing_mode(os.environ)
    except PartialSigningSecretsError as e:
        print(f"::error::{e}", file=sys.stderr)
        return 2
    print(mode)
    return 0


if __name__ == "__main__":
    sys.exit(main())

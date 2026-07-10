#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# roam-33: create a TEMPORARY signing keychain + notary key under $RUNNER_TEMP
# for the release job (tier-3, protected env). NO self-destructing trap — the
# material must outlive this script so the sign step can use it; a paired
# keychain_cleanup.sh (run in an always() workflow step) removes it. Nothing
# is echoed except the two resolved paths/identity, written to $GITHUB_ENV by
# the caller. Secrets arrive via env from the `release` Environment.
set -euo pipefail

: "${ROAMEX_DEVELOPER_ID_CERT_P12:?}"
: "${ROAMEX_DEVELOPER_ID_CERT_PASSWORD:?}"
: "${ROAMEX_NOTARY_PRIVATE_KEY:?}"
: "${RUNNER_TEMP:?RUNNER_TEMP must be set (GitHub-hosted/self-hosted runner)}"

WORK="${RUNNER_TEMP}/roamex-signing"
mkdir -p "$WORK"
KEYCHAIN="$WORK/roamex-signing.keychain-db"
NOTARY_KEY="$WORK/notary_key.p8"
KEYCHAIN_PW="$(openssl rand -hex 16)"
CERT_P12="$WORK/cert.p12"

printf '%s' "$ROAMEX_DEVELOPER_ID_CERT_P12" | base64 --decode > "$CERT_P12"
security create-keychain -p "$KEYCHAIN_PW" "$KEYCHAIN"
security set-keychain-settings -lut 21600 "$KEYCHAIN"
security unlock-keychain -p "$KEYCHAIN_PW" "$KEYCHAIN"
security import "$CERT_P12" -k "$KEYCHAIN" \
  -P "$ROAMEX_DEVELOPER_ID_CERT_PASSWORD" -T /usr/bin/codesign
security set-key-partition-list -S apple-tool:,apple:,codesign: \
  -k "$KEYCHAIN_PW" "$KEYCHAIN" >/dev/null
# Prepend our keychain to the search list so codesign finds the identity.
security list-keychains -d user -s "$KEYCHAIN" \
  $(security list-keychains -d user | sed 's/"//g')
rm -f "$CERT_P12"

# The identity CN (Developer ID Application: ... (TEAMID)) — not a secret, but
# quote it downstream: it contains spaces and parentheses.
IDENTITY="$(security find-identity -v -p codesigning "$KEYCHAIN" \
  | sed -n 's/.*"\(Developer ID Application.*\)".*/\1/p' | head -1)"

printf '%s' "$ROAMEX_NOTARY_PRIVATE_KEY" | base64 --decode > "$NOTARY_KEY"
chmod 600 "$NOTARY_KEY"

# Emit to $GITHUB_ENV directly (values quoted safe there); print nothing else.
{
  echo "ROAMEX_SIGN_IDENTITY=${IDENTITY}"
  echo "ROAMEX_NOTARY_KEY_PATH=${NOTARY_KEY}"
  echo "ROAMEX_SIGNING_KEYCHAIN=${KEYCHAIN}"
} >> "${GITHUB_ENV:?run inside a GitHub Actions step}"
echo "[ok] signing keychain provisioned under RUNNER_TEMP"

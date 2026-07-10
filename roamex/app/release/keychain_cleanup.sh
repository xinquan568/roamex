#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# roam-33: remove the temporary signing material. Run in an always() workflow
# step so secrets never persist on the runner, even if signing failed.
set -uo pipefail
WORK="${RUNNER_TEMP:-/tmp}/roamex-signing"
if [ -n "${ROAMEX_SIGNING_KEYCHAIN:-}" ]; then
  security delete-keychain "${ROAMEX_SIGNING_KEYCHAIN}" 2>/dev/null || true
fi
rm -rf "$WORK"
echo "[ok] signing material removed"

#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# roam-38: activate the version-controlled hooks (per-clone; run once). CI mirrors every check.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
chmod +x .githooks/* scripts/checks/*.py
git config core.hooksPath .githooks
echo "Roamux hooks active (core.hooksPath=.githooks): pre-commit, commit-msg, pre-push."

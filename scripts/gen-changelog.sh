#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# roam-40 (§7.9): render CHANGELOG.md from Conventional-Commit history via git-cliff + cliff.toml.
# Generate-on-demand (release job / maintainer) — the repo carries no perpetually-stale committed copy.
# Fails loudly if git-cliff is absent (never an empty/garbage changelog). --check prints unreleased to stdout.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
if ! command -v git-cliff >/dev/null 2>&1; then
  echo "gen-changelog: git-cliff not found on PATH. Install it (cargo install git-cliff, or a release" >&2
  echo "  binary from github.com/orhun/git-cliff/releases) and retry." >&2
  exit 1
fi
if [ "${1:-}" = "--check" ]; then
  exec git-cliff --config cliff.toml --unreleased
fi
git-cliff --config cliff.toml --output CHANGELOG.md "$@"
echo "gen-changelog: wrote CHANGELOG.md"

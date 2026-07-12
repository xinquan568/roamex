#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Roamux self-hosted runner provisioning (roam-36, plan §12.6 personal-machine v1).
#
#   provision_runner.sh --dry-run          print the exact actions; fetch/echo NO token
#   provision_runner.sh                    download + verify + configure (does NOT start it)
#
# Start (session):  cd "$RUNNER_DIR" && nohup ./run.sh >runner.log 2>&1 &
# Persist (operator choice, deeper mutation):  cd "$RUNNER_DIR" && ./svc.sh install && ./svc.sh start
# Decommission:  ./config.sh remove --token <removal-token>  AND unset vars.ROAMEX_CI_CHROMIUM_RUNNER
# Security posture + upgrade path: docs/ci/self-hosted-runner.md. Needs no elevated privileges.
set -euo pipefail

REPO="${ROAMEX_REPO:-xinquan568/roamex}"
RUNNER_DIR="${ROAMEX_RUNNER_DIR:-${HOME}/roamux-runner}"
RUNNER_NAME="${ROAMEX_RUNNER_NAME:-roamux-builder-1}"
LABELS="self-hosted,macos,chromium-builder"
RUNNER_VERSION="${ROAMEX_RUNNER_VERSION:-2.321.0}"
TARBALL="actions-runner-osx-arm64-${RUNNER_VERSION}.tar.gz"
URL="https://github.com/actions/runner/releases/download/v${RUNNER_VERSION}/${TARBALL}"

DRY=0
[ "${1:-}" = "--dry-run" ] && DRY=1

if [ "$DRY" = 1 ]; then
  echo "DRY-RUN: would provision GitHub Actions runner for ${REPO}"
  echo "DRY-RUN:   dir:    ${RUNNER_DIR}"
  echo "DRY-RUN:   name:   ${RUNNER_NAME}"
  echo "DRY-RUN:   labels: ${LABELS}"
  echo "DRY-RUN:   fetch:  ${URL}"
  echo "DRY-RUN:   config: ./config.sh --url https://github.com/${REPO} --token <registration-token-from-api> \\"
  echo "DRY-RUN:             --name ${RUNNER_NAME} --labels ${LABELS} --unattended --replace"
  echo "DRY-RUN: no token fetched, nothing downloaded, nothing configured."
  exit 0
fi

mkdir -p "${RUNNER_DIR}"
cd "${RUNNER_DIR}"
if [ ! -f "./config.sh" ]; then
  echo "downloading actions-runner ${RUNNER_VERSION}..."
  curl -sSfL -o "${TARBALL}" "${URL}"
  tar xzf "${TARBALL}"
  rm -f "${TARBALL}"
fi

echo "requesting a registration token for ${REPO} (repo admin)..."
TOKEN="$(gh api -X POST "repos/${REPO}/actions/runners/registration-token" --jq .token)"

./config.sh --url "https://github.com/${REPO}" --token "${TOKEN}" \
  --name "${RUNNER_NAME}" --labels "${LABELS}" --unattended --replace

echo "configured. Start it with:  cd ${RUNNER_DIR} && nohup ./run.sh >runner.log 2>&1 &"
echo "Remember: set the repo variable ROAMEX_CI_CHROMIUM_RUNNER=${RUNNER_NAME} to enable tier-2 jobs,"
echo "and write ${RUNNER_DIR}/.env with ROAMEX_CHROMIUM_SRC / ROAMEX_DEPOT_TOOLS / ROAMEX_CANONICAL_OVERLAY."

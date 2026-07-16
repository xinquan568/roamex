#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Roamux tier-2 CI job (roam-36, plan §12.6 personal-machine v1): warm-base incremental build + the
# Roamux test suites, run on the self-hosted runner. The shared base checkout is touched ONLY via the
# two declared, restored channels: (1) the overlay symlink (flipped to this job's checkout, restored
# by the EXIT trap), (2) the idempotent fail-loud patch runhook. No sudo; no secrets on this tier.
set -euo pipefail

SRC="${ROAMUX_CHROMIUM_SRC:-${HOME}/chromium/src}"
OUT="${ROAMUX_CI_OUT:-out/CI}"
DEPOT="${ROAMUX_DEPOT_TOOLS:-${HOME}/depot_tools}"
ROAMUX_CANONICAL_OVERLAY="${ROAMUX_CANONICAL_OVERLAY:?set in the runner .env — the operator overlay the base symlink is restored to}"

export PATH="${DEPOT}:${PATH}"
SECONDS=0

restore_overlay() {
  ln -sfn "${ROAMUX_CANONICAL_OVERLAY}" "${SRC}/roamux"
  echo "overlay symlink restored to ${ROAMUX_CANONICAL_OVERLAY}"
}
trap restore_overlay EXIT

echo "== tier-2 warm-base job: base=${SRC} out=${OUT} workspace=${GITHUB_WORKSPACE} =="

# Declared channel 1: point the base's overlay at THIS job's checkout.
ln -sfn "${GITHUB_WORKSPACE}/roamux" "${SRC}/roamux"

# Declared channel 2: the runhook (idempotent; fails loudly on conflict — the rebase signal).
python3 "${GITHUB_WORKSPACE}/roamux/build/apply_patches.py" --chromium-src "${SRC}"

# roam-132: the rebrand-channel's XTB-binding tests are GRIT-dependent, so tier-1 CI (no
# checkout) SKIPS them — yet that is where the load-bearing "translation still binds after
# re-key" assertions live. This runner HAS the checkout, so run them fail-not-skip
# (REQUIRE_GRIT=1 turns a skip into a failure). Hermetic (tmp fixtures) — runs before the
# hours-long build so a regression fails fast. Uses ${SRC} only to import GRIT read-only.
( cd "${GITHUB_WORKSPACE}" && REQUIRE_GRIT=1 ROAMUX_CHROMIUM_SRC="${SRC}" \
    python3 -m unittest roamux.build.tests.test_rebrand_strings )

# roam-97: the signed-release parts-path + config-seam tests exercise Chromium's
# real signing package (chrome/installer/mac/signing). Tier-1 CI (no checkout)
# SKIPS them, but that is where the load-bearing "Chromium get_parts() paths
# resolve against the post-rename bundle" and "the Roamux config actually reaches
# the pipeline" assertions live. This runner HAS the checkout, so run them
# fail-not-skip (REQUIRE_SIGNING_PARTS=1 turns a skip into a failure). Hermetic
# (tmp fixtures; no real codesign/notarize) — imports ${SRC} read-only.
( cd "${GITHUB_WORKSPACE}" && REQUIRE_SIGNING_PARTS=1 ROAMUX_CHROMIUM_SRC="${SRC}" \
    python3 -m unittest roamux.build.tests.test_release_signing )

# Warm CI build dir: APFS-clone the operator's warm out/Default on first use (copy-on-write).
cd "${SRC}"
if [ ! -d "${OUT}" ]; then
  echo "cloning warm build dir out/Default -> ${OUT} (APFS copy-on-write)"
  cp -Rc out/Default "${OUT}"
fi

autoninja -C "${OUT}" roamux_unittests roamux_browser_unittests roamux_browsertests
"${OUT}/roamux_unittests"
"${OUT}/roamux_browser_unittests"
# roam-6: the settings-surface DOM suite. Filtered to the roamux tests to keep tier-2 wall-clock
# sane while E1 is the only browser-test suite; widen as later epics add suites.
"${OUT}/roamux_browsertests" --gtest_filter="Roamux*"

# Staleness gate against this job's overlay.
python3 "${GITHUB_WORKSPACE}/roamux/build/check_override_staleness.py" \
  --chromium-src "${SRC}" --overlay "${GITHUB_WORKSPACE}/roamux"

echo "tier-2 job green in ${SECONDS}s (warm incremental)" | tee -a "${GITHUB_STEP_SUMMARY:-/dev/null}"

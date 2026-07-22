#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Roamux tier-2 CI job (roam-36, plan §12.6 personal-machine v1): warm-base incremental build + the
# Roamux test suites, run on the self-hosted runner. The shared base checkout is touched ONLY via the
# two declared, restored channels: (1) the overlay symlink (flipped to this job's checkout, restored
# by the EXIT trap), (2) the pristine-reconcile + idempotent fail-loud patch runhook (roam-175: the
# base's tracked state is CI-owned). No sudo; no secrets on this tier.
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

# Channel 2 precondition (roam-175, roam-160 postmortem): reconcile the base's tracked
# state to pristine. The runhook's stack simulator matches the tree only against
# prefixes of THIS checkout's stack — after a patch-rewriting/deleting PR the base
# still carries the PREVIOUS stack, which matches no prefix and fails the job in
# seconds. reset --hard, not `checkout -- .` (that restores from a possibly-staged
# index); clean drops files a superseded stack ADDED, sparing the overlay symlink
# (-e /roamux, untracked by design) and all ignored paths (no -x: out/CI and the
# warm caches live there); single -f never descends into nested git repos (the
# DEPS-managed submodules). Consequence, documented in docs/ci/self-hosted-runner.md:
# the base's tracked state is CI-owned — uncommitted local edits do not survive a run.
echo "reconciling base to pristine (drops any superseded stack state)"
git -C "${SRC}" reset --hard HEAD
git -C "${SRC}" clean -fd -e /roamux

# Declared channel 2: the runhook (idempotent; fails loudly on conflict — the rebase signal).
python3 "${GITHUB_WORKSPACE}/roamux/build/apply_patches.py" --chromium-src "${SRC}"

# roam-147: vendor Sparkle into this job's overlay before building. out/Default carries
# roamux_enable_sparkle=true, and since roam-140 the tier-2 targets (roamux_browsertests)
# link the Sparkle-backed updater — so the framework must be present at
# roamux/third_party/sparkle (gitignored; absent in a fresh CI checkout). Mirrors the
# release pipeline's "Vendor Sparkle" step; idempotent (a no-op once vendored, SHA-pinned).
python3 "${GITHUB_WORKSPACE}/roamux/build/fetch_sparkle.py"

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

# roam-195: run every suite with an EXPLICIT retry limit. The launcher silently zeroes
# retries when a --gtest_filter is passed outside bot mode (test_launcher.cc: "not in bot
# mode and filtered by flag ... Set reties to zero"), which left the filtered
# roamux_browsertests line — and only that line — with no retries at all, while the two
# unfiltered suites kept the built-in default of 1. Every recorded occurrence of the
# TaskEnvironment "CompleteShutdown took more than 30 seconds" teardown hang (roam-195)
# crashed a test in exactly that unprotected suite, failing the whole job over a
# post-assertion shutdown stall. The explicit switch is resolved BEFORE the filter branch,
# so it restores retries; 2 absorbs a stochastic hang (bounded: a persistently hanging test
# still fails after 3 attempts, ~30s each) without masking a deterministic failure — a
# retried test is reported as such in the launcher summary. This is mitigation, not a cure:
# roam-195 stays open for the underlying teardown stall.
RETRY_LIMIT="${ROAMUX_CI_RETRY_LIMIT:-2}"
"${OUT}/roamux_unittests" --test-launcher-retry-limit="${RETRY_LIMIT}"
"${OUT}/roamux_browser_unittests" --test-launcher-retry-limit="${RETRY_LIMIT}"
# roam-6: the settings-surface DOM suite. Filtered to the roamux tests to keep tier-2 wall-clock
# sane while E1 is the only browser-test suite; widen as later epics add suites.
"${OUT}/roamux_browsertests" --gtest_filter="Roamux*" --test-launcher-retry-limit="${RETRY_LIMIT}"

# Staleness gate against this job's overlay.
python3 "${GITHUB_WORKSPACE}/roamux/build/check_override_staleness.py" \
  --chromium-src "${SRC}" --overlay "${GITHUB_WORKSPACE}/roamux"

echo "tier-2 job green in ${SECONDS}s (warm incremental)" | tee -a "${GITHUB_STEP_SUMMARY:-/dev/null}"

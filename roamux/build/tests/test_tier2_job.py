# SPDX-License-Identifier: Apache-2.0
"""Hermetic checks on the tier-2 job script (roam-36) — the declared-channel discipline.

v1 acceptance (per the frozen analysis): the job may touch the shared warm base ONLY through the two
declared, restored channels — the overlay symlink and the patch runhook. This is structural
enforcement (discipline-plus-tests), explicitly not kernel/JIT isolation; see docs/ci/self-hosted-runner.md.
"""

import pathlib
import unittest

SCRIPT = pathlib.Path(__file__).resolve().parent.parent / "ci" / "tier2_job.sh"


class Tier2JobScriptTest(unittest.TestCase):
    def setUp(self):
        self.assertTrue(SCRIPT.exists(), f"missing {SCRIPT}")
        self.text = SCRIPT.read_text()
        self.code = "\n".join(l for l in self.text.splitlines() if not l.strip().startswith("#"))

    def test_strict_mode(self):
        self.assertIn("set -euo pipefail", self.text)

    def test_exit_trap_restores_canonical_overlay(self):
        # The symlink flip (channel 1) must be undone no matter how the job exits.
        self.assertIn("trap", self.code)
        trap_line = next(l for l in self.code.splitlines() if l.strip().startswith("trap"))
        self.assertIn("EXIT", trap_line)
        self.assertIn("restore_overlay", trap_line)
        self.assertIn('ln -sfn "${ROAMUX_CANONICAL_OVERLAY}"', self.code)

    def test_declared_channels_present(self):
        self.assertIn('ln -sfn "${GITHUB_WORKSPACE}/roamux"', self.code)  # channel 1: symlink flip
        self.assertIn("apply_patches.py", self.code)                       # channel 2: the runhook

    def test_base_reconciled_to_pristine_before_runhook(self):
        # roam-175 (roam-160 postmortem): the runhook's stack simulator matches the
        # tree only against prefixes of THIS checkout's stack, so after a
        # patch-rewriting/deleting PR the base still carries the PREVIOUS stack —
        # which matches no prefix and fails the job in seconds. The job must
        # reconcile the base's tracked state to pristine first: reset --hard (NOT
        # `checkout -- .`, which restores from a possibly-staged index), then a
        # clean that drops files a superseded stack ADDED while sparing the overlay
        # symlink (-e /roamux — untracked by design) and every ignored path (no -x:
        # out/CI and the warm caches live there). Single -f only: clean must never
        # descend into nested git repos (Chromium's DEPS-managed submodules).
        lines = self.code.splitlines()
        reset = next((i for i, l in enumerate(lines) if "reset --hard HEAD" in l),
                     None)
        clean = next((i for i, l in enumerate(lines) if "clean -fd -e /roamux" in l),
                     None)
        runhook = next(i for i, l in enumerate(lines) if "apply_patches.py" in l)
        self.assertIsNotNone(reset, "no `reset --hard HEAD` reconcile in the job")
        self.assertIsNotNone(clean, "no `clean -fd -e /roamux` reconcile in the job")
        self.assertLess(reset, runhook, "reconcile must precede the runhook")
        self.assertLess(clean, runhook, "reconcile must precede the runhook")
        self.assertNotIn("-x", lines[clean], "clean -x would nuke out/CI")
        self.assertNotIn("-ff", lines[clean], "clean -ff would enter submodules")

    def test_staleness_gate_runs(self):
        self.assertIn("check_override_staleness.py", self.code)

    def test_all_three_suites_build_and_run(self):
        # roam-6 (WB-CI): the browser-test suite joined the tier-2 gate; a regression to the
        # two-suite line must fail here, not silently in CI.
        self.assertIn(
            "roamux_unittests roamux_browser_unittests roamux_browsertests",
            self.code)
        for binary in ("roamux_unittests", "roamux_browser_unittests",
                       "roamux_browsertests"):
            self.assertIn('"${OUT}/%s"' % binary, self.code)

    def test_every_suite_run_sets_an_explicit_retry_limit(self):
        # roam-195: the launcher ZEROES its retry limit when a --gtest_filter is passed
        # outside bot mode (base/test/launcher/test_launcher.cc: "not in bot mode and
        # filtered by flag ... Set reties to zero"), so the filtered roamux_browsertests
        # line silently ran with NO retries while the two unfiltered suites kept the
        # default of 1. Every recorded teardown-timeout flake landed in exactly that
        # unprotected suite. The explicit flag is resolved BEFORE the filter branch, so
        # passing it restores retries; assert it on ALL THREE invocations so a later
        # --gtest_filter added to another suite cannot silently disarm them again.
        run_lines = [l for l in self.code.splitlines() if '"${OUT}/roamux' in l]
        self.assertEqual(3, len(run_lines), f"expected 3 suite runs, got {run_lines}")
        for line in run_lines:
            self.assertIn("--test-launcher-retry-limit=", line,
                          f"suite run without an explicit retry limit: {line.strip()}")

    def test_no_sudo_no_secret_use(self):
        self.assertNotIn("sudo", self.code)
        self.assertNotIn("secrets.", self.text)

    def test_wall_time_recorded(self):
        self.assertIn("GITHUB_STEP_SUMMARY", self.code)
        self.assertIn("SECONDS", self.code)

    def test_base_writes_only_via_declared_channels(self):
        # Every line that references the base checkout var must be one of the declared channels,
        # a read-only use, or the build-dir path (out/CI lives under the base by design).
        # "reset --hard" / "clean -fd" (roam-175): the channel-2 reconcile precondition —
        # exact-command markers, so no other git mutation of the base sneaks past.
        allowed_markers = ("ln -sfn", "apply_patches.py", "check_override_staleness.py", "--chromium-src",
                           "autoninja", "gn ", "cd ", "cp ", "OUT=", "SRC=", "echo", "test ", "[ ",
                           "reset --hard HEAD", "clean -fd -e /roamux")
        for line in self.code.splitlines():
            if "${SRC}" in line or "$SRC" in line:
                self.assertTrue(any(m in line for m in allowed_markers),
                                f"undeclared base access: {line.strip()}")

    def test_rebrand_binding_gate_runs_fail_not_skip(self):
        # roam-132 review: the GRIT-bound rebrand binding tests SKIP on tier-1 (no
        # checkout) — that is where the load-bearing "translation still binds after
        # re-key" assertions live. Tier-2 HAS the checkout, so it must run them
        # fail-not-skip (REQUIRE_GRIT=1) against the base's GRIT, from the overlay
        # root so the dotted test module resolves.
        self.assertIn("REQUIRE_GRIT=1", self.code)
        self.assertIn("test_rebrand_strings", self.code)
        self.assertIn('ROAMUX_CHROMIUM_SRC="${SRC}"', self.code)

    def test_signing_parts_gate_runs_fail_not_skip(self):
        # roam-97: the signed-release parts-path + config-seam tests exercise
        # Chromium's real signing package and SKIP on tier-1 (no checkout). Tier-2
        # HAS the checkout, so it must run them fail-not-skip
        # (REQUIRE_SIGNING_PARTS=1) against the base's signing package, from the
        # overlay root so the dotted test module resolves.
        self.assertIn("REQUIRE_SIGNING_PARTS=1", self.code)
        self.assertIn("test_release_signing", self.code)


if __name__ == "__main__":
    unittest.main()

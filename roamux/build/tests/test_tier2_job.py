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

    def test_no_sudo_no_secret_use(self):
        self.assertNotIn("sudo", self.code)
        self.assertNotIn("secrets.", self.text)

    def test_wall_time_recorded(self):
        self.assertIn("GITHUB_STEP_SUMMARY", self.code)
        self.assertIn("SECONDS", self.code)

    def test_base_writes_only_via_declared_channels(self):
        # Every line that references the base checkout var must be one of the declared channels,
        # a read-only use, or the build-dir path (out/CI lives under the base by design).
        allowed_markers = ("ln -sfn", "apply_patches.py", "check_override_staleness.py", "--chromium-src",
                           "autoninja", "gn ", "cd ", "cp ", "OUT=", "SRC=", "echo", "test ", "[ ")
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

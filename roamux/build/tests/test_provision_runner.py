# SPDX-License-Identifier: Apache-2.0
"""Hermetic checks on the runner provisioning script (roam-36) via its --dry-run mode."""

import pathlib
import subprocess
import unittest

SCRIPT = pathlib.Path(__file__).resolve().parent.parent / "ci" / "provision_runner.sh"


class ProvisionRunnerDryRunTest(unittest.TestCase):
    def setUp(self):
        self.assertTrue(SCRIPT.exists(), f"missing {SCRIPT}")

    def run_dry(self):
        return subprocess.run(["bash", str(SCRIPT), "--dry-run"],
                              capture_output=True, text=True)

    def test_dry_run_succeeds_and_prints_label_triple(self):
        result = self.run_dry()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("self-hosted,macos,chromium-builder", result.stdout)
        self.assertIn("DRY-RUN", result.stdout)

    def test_dry_run_never_fetches_or_echoes_tokens(self):
        result = self.run_dry()
        out = result.stdout + result.stderr
        self.assertNotIn("registration-token", out.replace(
            "<registration-token-from-api>", ""),  # the placeholder itself is allowed
            "dry-run must not fetch a real registration token")
        self.assertIn("<registration-token-from-api>", result.stdout,
                      "dry-run prints the placeholder, never a real token")

    def test_script_has_no_sudo(self):
        self.assertNotIn("sudo", SCRIPT.read_text())


if __name__ == "__main__":
    unittest.main()

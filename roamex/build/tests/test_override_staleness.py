# SPDX-License-Identifier: Apache-2.0
"""Hermetic tests for check_override_staleness.py — no Chromium checkout required (CI tier-1 safe).

Fixtures build a throwaway git repo standing in for the Chromium tree (tagged like a milestone pin)
plus an overlay dir with a chromium_src override.
"""

import json
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

SCRIPT = pathlib.Path(__file__).resolve().parent.parent / "check_override_staleness.py"

UPSTREAM_REL = "chrome/common/sample.h"


def git(cwd, *args):
    subprocess.run(["git", "-C", str(cwd), *args], check=True, capture_output=True)


class OverrideStalenessTest(unittest.TestCase):
    def setUp(self):
        self.tmp = pathlib.Path(tempfile.mkdtemp(prefix="roamex-staleness-"))
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)
        # Fake Chromium checkout, tagged like a milestone pin.
        self.src = self.tmp / "src"
        (self.src / "chrome/common").mkdir(parents=True)
        git(self.src, "init", "-q")
        git(self.src, "config", "user.email", "test@roamex")
        git(self.src, "config", "user.name", "test")
        (self.src / UPSTREAM_REL).write_text("// upstream v1\n#define SAMPLE 1\n")
        git(self.src, "add", ".")
        git(self.src, "commit", "-qm", "v1")
        git(self.src, "tag", "1.0.0.0")
        # Overlay with one override mirroring the upstream path.
        self.overlay = self.tmp / "overlay"
        (self.overlay / "chromium_src/chrome/common").mkdir(parents=True)
        (self.overlay / "chromium_src" / UPSTREAM_REL).write_text(
            "// upstream v1\n#define SAMPLE 1\n#define ROAMEX_OVERRIDE 1\n")
        self.pin_file = self.tmp / "CHROMIUM_PIN"
        self.pin_file.write_text("# comment line\n1.0.0.0\n")
        self.manifest = self.tmp / "override_signatures.json"

    def run_check(self, *extra):
        return subprocess.run(
            [sys.executable, str(SCRIPT), "--chromium-src", str(self.src),
             "--overlay", str(self.overlay), "--pin-file", str(self.pin_file),
             "--manifest", str(self.manifest), *extra],
            capture_output=True, text=True)

    def _uprev_with_mutation(self):
        (self.src / UPSTREAM_REL).write_text("// upstream v2 CHANGED\n#define SAMPLE 2\n")
        git(self.src, "commit", "-aqm", "v2")
        git(self.src, "tag", "2.0.0.0")
        self.pin_file.write_text("2.0.0.0\n")

    def test_update_records_then_clean_pass(self):
        result = self.run_check("--update")
        self.assertEqual(result.returncode, 0, result.stderr)
        recorded = json.loads(self.manifest.read_text())
        self.assertEqual(recorded["pin"], "1.0.0.0")
        self.assertIn(UPSTREAM_REL, recorded["signatures"])
        result = self.run_check()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_uprev_mutation_fails_loud(self):
        self.assertEqual(self.run_check("--update").returncode, 0)
        self._uprev_with_mutation()
        result = self.run_check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(UPSTREAM_REL, result.stdout + result.stderr)

    def test_unregistered_override_fails(self):
        self.assertEqual(self.run_check("--update").returncode, 0)
        extra_rel = "chrome/common/other.h"
        (self.src / extra_rel).write_text("// upstream other\n")
        git(self.src, "add", ".")
        git(self.src, "commit", "-qm", "add other")
        git(self.src, "tag", "-f", "1.0.0.0")  # same pin now contains the file
        (self.overlay / "chromium_src" / extra_rel).write_text("// override other\n")
        result = self.run_check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(extra_rel, result.stdout + result.stderr)

    def test_signatures_come_from_git_show_not_working_tree(self):
        # Records, then mutates the upstream file WITHOUT committing or retagging: the gate must
        # still pass because signatures read pristine content at the tag, never the working tree.
        self.assertEqual(self.run_check("--update").returncode, 0)
        (self.src / UPSTREAM_REL).write_text("// locally patched working tree (e.g. runhook output)\n")
        result = self.run_check()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_pin_must_be_a_tag_not_a_commitish(self):
        # A commit-ish that is not a milestone tag (e.g. HEAD) must NOT satisfy the pin gate.
        self.assertEqual(self.run_check("--update").returncode, 0)
        self.pin_file.write_text("HEAD\n")
        result = self.run_check()
        self.assertNotEqual(result.returncode, 0)

    def test_unresolvable_pin_hard_fails_with_fetch_hint(self):
        self.assertEqual(self.run_check("--update").returncode, 0)
        self.pin_file.write_text("3.0.0.0\n")  # tag does not exist
        result = self.run_check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("fetch --depth=1 origin", result.stdout + result.stderr)

    def test_unpinned_hard_fails(self):
        self.pin_file.write_text("# not pinned yet\nUNPINNED\n")
        result = self.run_check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("UNPINNED", result.stdout + result.stderr)

    def test_missing_manifest_fails(self):
        result = self.run_check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("--update", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()

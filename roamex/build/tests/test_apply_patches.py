# SPDX-License-Identifier: Apache-2.0
"""Hermetic tests for apply_patches.py — no Chromium checkout required (CI tier-1 safe).

Fixtures build a throwaway git repo standing in for the Chromium tree, plus a patches dir.
"""

import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

SCRIPT = pathlib.Path(__file__).resolve().parent.parent / "apply_patches.py"

PATCH_ADD_MARKER = """--- a/afile.txt
+++ b/afile.txt
@@ -1,3 +1,4 @@
 line1
+MARKER
 line2
 line3
"""


def git(cwd, *args):
    subprocess.run(["git", "-C", str(cwd), *args], check=True, capture_output=True)


class ApplyPatchesTest(unittest.TestCase):
    def setUp(self):
        self.tmp = pathlib.Path(tempfile.mkdtemp(prefix="roamex-runhook-"))
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)
        self.src = self.tmp / "src"
        self.src.mkdir()
        git(self.src, "init", "-q")
        git(self.src, "config", "user.email", "test@roamex")
        git(self.src, "config", "user.name", "test")
        (self.src / "afile.txt").write_text("line1\nline2\nline3\n")
        git(self.src, "add", ".")
        git(self.src, "commit", "-qm", "init")
        self.patches = self.tmp / "patches"
        self.patches.mkdir()
        (self.patches / "0001-add-marker.patch").write_text(PATCH_ADD_MARKER)

    def run_hook(self, *extra):
        return subprocess.run(
            [sys.executable, str(SCRIPT), "--chromium-src", str(self.src),
             "--patches", str(self.patches), *extra],
            capture_output=True, text=True)

    def test_apply_fresh(self):
        result = self.run_hook()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("MARKER", (self.src / "afile.txt").read_text())

    def test_idempotent_second_run_is_noop(self):
        self.assertEqual(self.run_hook().returncode, 0)
        result = self.run_hook()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.src / "afile.txt").read_text().count("MARKER"), 1)
        self.assertIn("applied", result.stdout)  # reported as already-applied, not re-applied

    def test_conflict_fails_loud_naming_the_patch(self):
        (self.src / "afile.txt").write_text("entirely\ndifferent\ncontent\n")
        git(self.src, "commit", "-aqm", "diverge")
        result = self.run_hook()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("0001-add-marker.patch", result.stdout + result.stderr)

    def test_check_mode_does_not_mutate(self):
        result = self.run_hook("--check")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("MARKER", (self.src / "afile.txt").read_text())

    def test_check_mode_fails_on_conflict(self):
        (self.src / "afile.txt").write_text("entirely\ndifferent\ncontent\n")
        git(self.src, "commit", "-aqm", "diverge")
        result = self.run_hook("--check")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("0001-add-marker.patch", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()

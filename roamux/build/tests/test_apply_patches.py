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


def _git_env():
    import os
    return {k: v for k, v in os.environ.items()
            if k not in ("GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_PREFIX",
                         "GIT_COMMON_DIR", "GIT_OBJECT_DIRECTORY")}


def git(cwd, *args):
    subprocess.run(["git", "-C", str(cwd), *args], check=True, capture_output=True,
                   env=_git_env())


class ApplyPatchesTest(unittest.TestCase):
    def setUp(self):
        self.tmp = pathlib.Path(tempfile.mkdtemp(prefix="roamux-runhook-"))
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)
        self.src = self.tmp / "src"
        self.src.mkdir()
        git(self.src, "init", "-q")
        git(self.src, "config", "user.email", "test@roamux")
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


# roam-77: a later patch inserting a line ADJACENT to an earlier patch's
# insertion breaks the earlier patch's reverse-context on the fully-applied
# tree (the 0008x0020 / 0006x0024 field shape). The detector must reason about
# the stack as a whole, not per-patch.
PATCH_ADJACENT_MARKER_B = """--- a/afile.txt
+++ b/afile.txt
@@ -1,3 +1,4 @@
 line1
+MARKER-B
 MARKER
 line2
"""


class AdjacentInsertStackTest(unittest.TestCase):
    def setUp(self):
        self.tmp = pathlib.Path(tempfile.mkdtemp(prefix="roamux-runhook-adj-"))
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)
        self.src = self.tmp / "src"
        self.src.mkdir()
        git(self.src, "init", "-q")
        git(self.src, "config", "user.email", "test@roamux")
        git(self.src, "config", "user.name", "test")
        (self.src / "afile.txt").write_text("line1\nline2\nline3\n")
        git(self.src, "add", ".")
        git(self.src, "commit", "-qm", "init")
        self.patches = self.tmp / "patches"
        self.patches.mkdir()
        (self.patches / "0001-add-marker.patch").write_text(PATCH_ADD_MARKER)
        (self.patches / "0002-add-marker-b.patch").write_text(
            PATCH_ADJACENT_MARKER_B)

    def run_hook(self, *extra):
        return subprocess.run(
            [sys.executable, str(SCRIPT), "--chromium-src", str(self.src),
             "--patches", str(self.patches), *extra],
            capture_output=True, text=True)

    def afile(self):
        return (self.src / "afile.txt").read_text()

    def test_fresh_check_and_apply(self):
        result = self.run_hook("--check")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("MARKER", self.afile())
        result = self.run_hook()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.afile(),
                         "line1\nMARKER-B\nMARKER\nline2\nline3\n")

    def test_check_passes_on_fully_applied_stack(self):
        # The field failure: per-patch reverse-check reads 0001 as neither
        # applied nor appliable once 0002's line sits inside its context.
        self.assertEqual(self.run_hook().returncode, 0)
        result = self.run_hook("--check")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout.count("[applied]"), 2, result.stdout)

    def test_apply_is_idempotent_on_fully_applied_stack(self):
        self.assertEqual(self.run_hook().returncode, 0)
        before = self.afile()
        result = self.run_hook()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.afile(), before)
        self.assertEqual(self.afile().count("MARKER-B"), 1)

    def test_prefix_apply_completes_the_stack(self):
        git(self.src, "apply", str(self.patches / "0001-add-marker.patch"))
        result = self.run_hook()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.afile(),
                         "line1\nMARKER-B\nMARKER\nline2\nline3\n")

    def test_prefix_check_reports_and_does_not_mutate(self):
        git(self.src, "apply", str(self.patches / "0001-add-marker.patch"))
        before = self.afile()
        result = self.run_hook("--check")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("[applied]", result.stdout)
        self.assertIn("[appliable]", result.stdout)
        self.assertEqual(self.afile(), before)

    def test_relative_patches_dir_works(self):
        # The scratch simulation changes cwd for git apply; a relative
        # --patches path must still resolve (roam-77 Step-8 regression).
        import os
        rel = os.path.relpath(self.patches, os.getcwd())
        result = subprocess.run(
            [sys.executable, str(SCRIPT), "--chromium-src", str(self.src),
             "--patches", rel, "--check"],
            capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout.count("[appliable]"), 2, result.stdout)

    def test_diverged_tree_fails_loud_both_modes(self):
        self.assertEqual(self.run_hook().returncode, 0)
        with (self.src / "afile.txt").open("a") as f:
            f.write("local-edit\n")
        for extra in (["--check"], []):
            result = self.run_hook(*extra)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("afile.txt", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()

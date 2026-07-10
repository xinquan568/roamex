# SPDX-License-Identifier: Apache-2.0
"""Hermetic tests for fetch_sparkle.py — no network (roam-32, R16).

A local fixture archive stands in for the pinned release; the hash gate must
reject a mismatch BEFORE extraction and the fetch must be idempotent.
"""

import hashlib
import pathlib
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest

SCRIPT = pathlib.Path(__file__).resolve().parent.parent / "fetch_sparkle.py"


class FetchSparkleTest(unittest.TestCase):
    def setUp(self):
        self.tmp = pathlib.Path(tempfile.mkdtemp(prefix="roamex-sparkle-t-"))
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)
        payload = self.tmp / "payload"
        (payload / "Sparkle.framework" / "Versions" / "B").mkdir(parents=True)
        (payload / "Sparkle.framework" / "Versions" / "B" / "Sparkle").write_text("stub")
        (payload / "LICENSE").write_text("test license")
        self.archive = self.tmp / "fixture.tar.xz"
        with tarfile.open(self.archive, "w:xz") as tar:
            for child in sorted(payload.iterdir()):
                tar.add(child, arcname=child.name)
        self.sha256 = hashlib.sha256(self.archive.read_bytes()).hexdigest()
        self.dest = self.tmp / "vendor"

    def run_fetch(self, *extra):
        return subprocess.run(
            [sys.executable, str(SCRIPT), "--dest", str(self.dest),
             "--archive", str(self.archive), *extra],
            capture_output=True, text=True)

    def test_wrong_hash_refuses_before_extraction(self):
        result = self.run_fetch("--expected-sha256", "0" * 64)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("SHA-256 mismatch", result.stderr)
        self.assertFalse((self.dest / "Sparkle.framework").exists())

    def test_correct_hash_extracts_and_is_idempotent(self):
        result = self.run_fetch("--expected-sha256", self.sha256)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(
            (self.dest / "Sparkle.framework" / "Versions" / "B" / "Sparkle").is_file())
        self.assertTrue((self.dest / "LICENSE").is_file())
        again = self.run_fetch("--expected-sha256", self.sha256)
        self.assertEqual(again.returncode, 0)
        self.assertIn("already present", again.stdout)


if __name__ == "__main__":
    unittest.main()

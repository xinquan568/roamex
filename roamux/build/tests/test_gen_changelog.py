# SPDX-License-Identifier: Apache-2.0
"""Tests for the git-cliff changelog generator (roam-40). The end-to-end render runs the real git-cliff
when present and skips-with-note when absent (honest degradation, roam-38's pattern). The config +
wrapper are checked structurally, hermetically.
"""

import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
SCRIPT = REPO_ROOT / "scripts" / "gen-changelog.sh"
CLIFF = REPO_ROOT / "cliff.toml"

# roam-38's enforced Conventional-Commit types — cliff.toml must map every one to a group.
ROAM38_TYPES = ["feat", "fix", "chore", "docs", "test", "refactor", "perf", "build", "ci", "style",
                "revert"]


def _clean_git_env():
    return {k: v for k, v in os.environ.items()
            if k not in ("GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_PREFIX",
                         "GIT_COMMON_DIR", "GIT_OBJECT_DIRECTORY")}


def git(cwd, *args):
    subprocess.run(["git", "-C", str(cwd), *args], check=True, capture_output=True, env=_clean_git_env())


class CliffConfigTest(unittest.TestCase):
    def test_cliff_covers_all_roam38_types(self):
        text = CLIFF.read_text()
        for t in ROAM38_TYPES:
            self.assertIn(f'message = "^{t}"', text, f"cliff.toml has no parser for '{t}'")

    def test_cliff_is_conventional_and_filters_unconventional(self):
        text = CLIFF.read_text()
        self.assertIn("conventional_commits = true", text)
        self.assertIn("filter_unconventional = true", text)


class GenChangelogWrapperTest(unittest.TestCase):
    def test_fails_loud_when_tool_missing(self):
        text = SCRIPT.read_text()
        self.assertIn('command -v git-cliff', text)
        self.assertIn("exit 1", text)
        self.assertIn("Install", text)

    def test_no_committed_changelog(self):
        # generate-on-demand policy: the repo must not carry a perpetually-stale CHANGELOG.md.
        self.assertFalse((REPO_ROOT / "CHANGELOG.md").exists(),
                         "CHANGELOG.md is generate-on-demand; do not commit a stale copy")


class GenChangelogEndToEndTest(unittest.TestCase):
    def setUp(self):
        if not shutil.which("git-cliff"):
            self.skipTest("git-cliff not installed — end-to-end render deferred to a runner that has it")
        self.tmp = pathlib.Path(tempfile.mkdtemp(prefix="roamux-cliff-"))
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)

    def _sample_repo(self):
        d = self.tmp
        git(d, "init", "-q")
        git(d, "config", "user.email", "t@roamux"); git(d, "config", "user.name", "t")
        # gen-changelog.sh reads cliff.toml at the repo root and installs the hooksPath is irrelevant.
        shutil.copy(CLIFF, d / "cliff.toml")
        shutil.copy(SCRIPT, d / "gen-changelog.sh")
        (d / "a").write_text("1")
        git(d, "add", "."); git(d, "commit", "-qm", "feat(x): add a thing (roam-1)")
        (d / "b").write_text("2")
        git(d, "add", "."); git(d, "commit", "-qm", "fix(y): correct a thing (roam-2)")
        git(d, "tag", "v0.1.0")
        return d

    def test_wrapper_writes_versioned_grouped_changelog(self):
        # Full render via the ACTUAL wrapper — the tagged release section carries the version heading.
        d = self._sample_repo()
        r = subprocess.run(["bash", "gen-changelog.sh"], cwd=d,
                           capture_output=True, text=True, env=_clean_git_env())
        self.assertEqual(r.returncode, 0, r.stderr)
        cl = (d / "CHANGELOG.md").read_text()
        import re
        # the release heading carries BOTH the version and an ISO date: "## [<ver>] - YYYY-MM-DD"
        self.assertRegex(cl, r"## \[[^\]]*0\.1\.0[^\]]*\] - \d{4}-\d{2}-\d{2}")
        self.assertIn("Features", cl)
        self.assertIn("Bug Fixes", cl)
        self.assertIn("add a thing", cl.lower())

    def test_wrapper_check_prints_unreleased(self):
        # --check dry-run: an untagged commit after the tag lands in the unreleased section.
        d = self._sample_repo()
        (d / "c").write_text("3")
        git(d, "add", "."); git(d, "commit", "-qm", "docs: note something (roam-3)")
        r = subprocess.run(["bash", "gen-changelog.sh", "--check"], cwd=d,
                           capture_output=True, text=True, env=_clean_git_env())
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("## [unreleased]", r.stdout)
        self.assertIn("Documentation", r.stdout)
        self.assertFalse((d / "CHANGELOG.md").exists(), "--check must not write the file")

    def test_wrapper_fails_loud_without_git_cliff(self):
        # PATH scrubbed of git-cliff → the wrapper must exit non-zero with an install hint.
        d = self._sample_repo()
        env = {"PATH": "/usr/bin:/bin", "HOME": str(d)}
        r = subprocess.run(["bash", "gen-changelog.sh"], cwd=d, capture_output=True, text=True, env=env)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("Install", r.stderr + r.stdout)


if __name__ == "__main__":
    unittest.main()

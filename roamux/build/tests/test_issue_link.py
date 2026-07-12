# SPDX-License-Identifier: Apache-2.0
"""Hermetic tests for the roam-N issue-link gate (roam-39). The consistency core runs offline (no
--repo); the fail-closed open-issue lookup is asserted structurally (never reaches the network).
"""

import pathlib
import subprocess
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
SCRIPT = REPO_ROOT / "scripts" / "checks" / "check-issue-link.sh"
WORKFLOW = REPO_ROOT / ".github" / "workflows" / "issue-link.yml"

OK_BRANCH = "xinquan568/ai/roam-12-some-slug"
OK_TITLE = "feat(x): do the thing (roam-12)"
OK_BODY = "## Summary\n- x\n\n## Closes\nCloses #12\n"


def run(branch=OK_BRANCH, title=OK_TITLE, body=OK_BODY, repo=None):
    args = ["bash", str(SCRIPT), "--branch", branch, "--title", title, "--body", body]
    if repo is not None:
        args += ["--repo", repo]
    return subprocess.run(args, capture_output=True, text=True)


class ConsistencyTest(unittest.TestCase):
    def test_consistent_triple_passes(self):
        r = run()
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_branch_missing_fails(self):
        r = run(branch="xinquan568/ai/feature-no-id")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("branch", r.stderr)

    def test_title_missing_terminal_tag_fails(self):
        r = run(title="feat(x): do the thing")  # no (roam-12)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("title", r.stderr)

    def test_body_missing_closes_fails(self):
        r = run(body="## Summary\n- x\n")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("body", r.stderr)

    def test_numeric_mismatch_fails(self):
        r = run(branch="xinquan568/ai/roam-120-slug")  # 120 vs 12/12
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("mismatch", r.stderr)

    def test_branch_roam_no_number_fails(self):
        self.assertNotEqual(run(branch="xinquan568/ai/roam-nope-slug").returncode, 0)

    def test_branch_roam_12x_fails(self):
        self.assertNotEqual(run(branch="xinquan568/ai/roam-12x-slug").returncode, 0)

    def test_title_roamux_not_roam_n_fails(self):
        self.assertNotEqual(run(title="feat: x (roamux)").returncode, 0)

    def test_body_closes_12x_rejected(self):
        # 'Closes #12x' references #12x, not #12 — must not extract 12.
        self.assertNotEqual(run(body="Closes #12x").returncode, 0)

    def test_body_encloses_substring_rejected(self):
        # 'Encloses #12' contains 'closes #12' but is not a real close directive.
        self.assertNotEqual(run(body="Encloses #12").returncode, 0)

    def test_body_empty_rejected(self):
        self.assertNotEqual(run(body="").returncode, 0)

    def test_title_tag_not_terminal_fails(self):
        r = run(title="feat: (roam-12) trailing words")  # tag not at end
        self.assertNotEqual(r.returncode, 0)

    def test_closes_case_insensitive(self):
        r = run(body="closes #12")
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_bare_roam_without_slug_fails(self):
        # The branch surface requires the .../roam-<N>-<slug> shape; a bare roam-12 (no trailing -slug)
        # is not a valid work branch and must fail.
        r = run(branch="xinquan568/ai/roam-12")
        self.assertNotEqual(r.returncode, 0)


class FailClosedStructureTest(unittest.TestCase):
    def setUp(self):
        self.assertTrue(SCRIPT.exists(), f"missing {SCRIPT}")
        self.text = SCRIPT.read_text()

    def test_lookup_only_with_repo(self):
        self.assertIn('if [ -n "$REPO" ]', self.text)

    def test_gh_api_fails_closed(self):
        self.assertIn("gh api", self.text)
        # the lookup must `|| fail` (no silent skip) and reject PRs / non-open state
        self.assertIn("|| fail", self.text)
        self.assertIn('has("pull_request")', self.text)
        self.assertIn("pull request, not an issue", self.text)
        self.assertIn("open", self.text)


class WorkflowStructureTest(unittest.TestCase):
    def setUp(self):
        self.assertTrue(WORKFLOW.exists(), f"missing {WORKFLOW}")
        self.text = WORKFLOW.read_text()

    def test_pull_request_trigger_only(self):
        self.assertIn("pull_request", self.text)
        self.assertNotIn("\n  push:", self.text)

    def test_reads_pr_event_fields_not_merge_commit(self):
        self.assertIn("github.event.pull_request.head.ref", self.text)
        self.assertIn("github.event.pull_request.title", self.text)
        self.assertIn("github.event.pull_request.body", self.text)

    def test_passes_repo_for_the_live_lookup(self):
        self.assertIn("--repo", self.text)

    def test_spdx(self):
        self.assertIn("SPDX-License-Identifier: Apache-2.0", "\n".join(self.text.splitlines()[:3]))


if __name__ == "__main__":
    unittest.main()

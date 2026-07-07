# SPDX-License-Identifier: Apache-2.0
"""Hermetic CI-workflow invariants (roam-5, plan §12.6) — no Chromium checkout, no external deps.

These encode the tier-1 + release posture structurally, so every future workflow edit is gated:
  1. No workflow uses `pull_request_target` (the fork-secrets foot-gun).
  2. ci.yml has the stable `lint` job and it references no secrets (fork/tier-1 boundary is
     structural: fork isolation is GitHub's platform guarantee, preserved by structure).
  3. Chromium-dependent jobs are explicitly marked (ROAMEX_CHROMIUM_DEPENDENT) and gated: in ci.yml
     on BOTH the capability variable AND the non-fork condition (R15 — fork PRs stay tier-1 even
     after a capable runner exists); in nightly.yml on the capability variable.
  4. release.yml binds `environment: release` and triggers only on v* tags / manual dispatch.
  5. nightly.yml is scheduled.
"""

import pathlib
import unittest

WORKFLOWS = pathlib.Path(__file__).resolve().parents[3] / ".github" / "workflows"

CAPABILITY_VAR = "ROAMEX_CI_CHROMIUM_RUNNER"
MARKER = "ROAMEX_CHROMIUM_DEPENDENT"
FORK_CONDITION = "head.repo.fork"


def _read(name):
    path = WORKFLOWS / name
    if not path.exists():
        return None
    return path.read_text()


def _marked_job_blocks(text):
    """Split a workflow into chunks per marked (Chromium-dependent) job, marker line included."""
    blocks, current, capturing = [], [], False
    for line in text.splitlines():
        if MARKER in line:
            if capturing and current:
                blocks.append("\n".join(current))
            current, capturing = [line], True
            continue
        if capturing:
            # A new top-level job key (two-space indent, ends with ':') closes the block.
            if line.startswith("  ") and not line.startswith("   ") and line.rstrip().endswith(":"):
                blocks.append("\n".join(current))
                current, capturing = [], False
            else:
                current.append(line)
    if capturing and current:
        blocks.append("\n".join(current))
    return blocks


class WorkflowInvariantsTest(unittest.TestCase):
    def test_no_pull_request_target_anywhere(self):
        self.assertTrue(WORKFLOWS.is_dir(), f"missing {WORKFLOWS}")
        for wf in sorted(WORKFLOWS.glob("*.yml")):
            self.assertNotIn("pull_request_target", wf.read_text(),
                             f"{wf.name} uses the pull_request_target foot-gun")

    def test_ci_lint_job_exists_and_references_no_secrets(self):
        text = _read("ci.yml")
        self.assertIsNotNone(text, "ci.yml missing")
        self.assertIn("\n  lint:", text, "the stable `lint` job is load-bearing (required check)")
        # The tier-1 boundary is structural: nothing in ci.yml may reference secrets at all.
        self.assertNotIn("secrets.", text,
                         "ci.yml runs for fork PRs — it must reference no secrets")

    def test_ci_has_gated_targeted_suite(self):
        text = _read("ci.yml")
        self.assertIsNotNone(text, "ci.yml missing")
        blocks = _marked_job_blocks(text)
        self.assertTrue(blocks, "ci.yml must contain a marked Chromium-dependent targeted-suite job")
        for block in blocks:
            self.assertIn(CAPABILITY_VAR, block,
                          "Chromium-dependent job lacks the capability gate")
            self.assertIn(FORK_CONDITION, block,
                          "Chromium-dependent PR job lacks the non-fork condition (R15)")
            # Structural precedence (S8-1): comments don't count — the gate's run body must handle
            # the fork path (writing enabled=false) BEFORE any path can write enabled=true.
            code_lines = [l for l in block.splitlines() if not l.strip().startswith("#")]
            code = "\n".join(code_lines)
            fork_branch = code.find('if [ "$IS_FORK" = "true" ]')
            self.assertNotEqual(fork_branch, -1,
                                "gate run body must branch on IS_FORK (not merely mention it)")
            enable_write = code.find("enabled=true")
            self.assertNotEqual(enable_write, -1, "gate must be able to enable when appropriate")
            self.assertLess(fork_branch, enable_write,
                            "the fork check must precede the capability-enabled path (R15)")
            fork_block = code[fork_branch:code.find("elif", fork_branch)]
            self.assertIn("enabled=false", fork_block,
                          "the fork branch must write enabled=false")

    def test_nightly_scheduled_and_gated(self):
        text = _read("nightly.yml")
        self.assertIsNotNone(text, "nightly.yml missing")
        self.assertIn("schedule:", text, "nightly must be scheduled")
        blocks = _marked_job_blocks(text)
        self.assertTrue(blocks, "nightly.yml must mark its Chromium-dependent work")
        for block in blocks:
            self.assertIn(CAPABILITY_VAR, block,
                          "nightly Chromium work lacks the capability gate")

    def test_release_binds_environment_and_tag_triggers_only(self):
        text = _read("release.yml")
        self.assertIsNotNone(text, "release.yml missing")
        self.assertIn("environment: release", text,
                      "release job must bind the protected Environment")
        # Structural on:-block parsing (S8-2): triggers are ONLY push.tags v* + workflow_dispatch.
        lines = text.splitlines()
        on_start = next(i for i, l in enumerate(lines) if l.rstrip() == "on:")
        on_block = []
        for line in lines[on_start + 1:]:
            if line.strip() and not line.startswith(" "):
                break  # next top-level key ends the on: block
            on_block.append(line)
        on_text = "\n".join(on_block)
        triggers = [l.strip().rstrip(":") for l in on_block
                    if l.startswith("  ") and not l.startswith("   ") and l.strip().endswith(":")]
        self.assertEqual(sorted(triggers), ["push", "workflow_dispatch"],
                         f"release triggers must be exactly push+workflow_dispatch, got {triggers}")
        self.assertIn("tags:", on_text, "release push trigger must be tag-scoped")
        self.assertNotIn("branches", on_text, "release must not trigger on branch pushes")
        tag_patterns = [l.strip().lstrip("- ").strip('"') for l in on_block if l.strip().startswith("- ")]
        self.assertEqual(tag_patterns, ["v*"], f"release tags must be exactly v*, got {tag_patterns}")

    def test_workflows_carry_spdx(self):
        for wf in sorted(WORKFLOWS.glob("*.yml")):
            head = "\n".join(wf.read_text().splitlines()[:3])
            self.assertIn("SPDX-License-Identifier: Apache-2.0", head,
                          f"{wf.name} missing SPDX header")


if __name__ == "__main__":
    unittest.main()

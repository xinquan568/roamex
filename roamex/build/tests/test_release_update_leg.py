# SPDX-License-Identifier: Apache-2.0
"""roam-34: release.yml update-leg invariants — the write scope, the
every-mode Sparkle gate, and the draft-create -> staging-validate -> publish
ordering with --latest. Text-level (no YAML dep, matching the repo's other
workflow-invariant tests)."""

import pathlib
import unittest

RELEASE = (pathlib.Path(__file__).resolve().parents[3] / ".github" /
           "workflows" / "release.yml")


class ReleaseUpdateLegTest(unittest.TestCase):
    def setUp(self):
        self.text = RELEASE.read_text()

    def test_has_contents_write_permission(self):
        self.assertIn("contents: write", self.text)

    def test_sign_step_is_not_gated_on_apple_mode(self):
        # K3: the Sparkle sign/appcast leg must NOT be conditioned on
        # steps.mode == signed.
        i = self.text.index("Sign updates + generate appcast")
        block = self.text[i:i + 400]
        self.assertNotIn("mode == 'signed'", block)
        self.assertIn("SPARKLE_ED_PRIVATE_KEY", block)

    def test_draft_before_validate_before_publish_ordering(self):
        i_draft = self.text.index("--draft --prerelease=false")
        i_validate = self.text.index("verify_appcast.py")
        i_publish = self.text.index("--draft=false --prerelease=false --latest")
        self.assertLess(i_draft, i_validate,
                        "draft must be created before staging validation")
        self.assertLess(i_validate, i_publish,
                        "staging validation must precede publish")

    def test_publish_marks_latest(self):
        self.assertIn("--latest", self.text)


if __name__ == "__main__":
    unittest.main()

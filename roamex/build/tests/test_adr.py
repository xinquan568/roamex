# SPDX-License-Identifier: Apache-2.0
"""Structural tests for the ADR docs (roam-40) — hermetic, no tools."""

import pathlib
import unittest

ADR = pathlib.Path(__file__).resolve().parents[3] / "docs" / "adr"


class AdrTemplateTest(unittest.TestCase):
    def test_template_has_the_canonical_sections(self):
        text = (ADR / "0000-template.md").read_text()
        for section in ("## Status", "## Context", "## Decision", "## Consequences"):
            self.assertIn(section, text, f"template missing {section}")

    def test_readme_documents_numbering(self):
        text = (ADR / "README.md").read_text()
        self.assertIn("NNNN-slug", text)
        self.assertIn("Status", text)

    def test_first_adr_uses_the_template(self):
        text = (ADR / "0001-chromium-overlay-strategy.md").read_text()
        for section in ("## Status", "## Context", "## Decision", "## Consequences"):
            self.assertIn(section, text)
        self.assertIn("Accepted", text)  # a decided ADR

    def test_adrs_are_numbered_nnnn_slug(self):
        import re
        for f in ADR.glob("[0-9]*.md"):
            self.assertRegex(f.name, r"^\d{4}-[a-z0-9][a-z0-9-]*\.md$", f.name)


if __name__ == "__main__":
    unittest.main()

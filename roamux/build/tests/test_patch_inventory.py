# SPDX-License-Identifier: Apache-2.0
"""Hermetic inventory-completeness check (roam-78): every declared patch under
roamux/patches/ must carry a row in the README inventory (the repo-local
mirror of the plan §12.2 upstream-hook inventory). Name-presence only — row
content stays a human review surface, not test churn."""

import pathlib
import unittest

PATCHES_DIR = pathlib.Path(__file__).resolve().parent.parent.parent / "patches"


class PatchInventoryTest(unittest.TestCase):
    def test_every_patch_has_a_readme_inventory_row(self):
        readme = (PATCHES_DIR / "README.md").read_text()
        missing = [p.name for p in sorted(PATCHES_DIR.glob("*.patch"))
                   if p.name not in readme]
        self.assertEqual(missing, [],
                         f"patches without a README inventory row: {missing}")


if __name__ == "__main__":
    unittest.main()

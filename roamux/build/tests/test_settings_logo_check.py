# SPDX-License-Identifier: Apache-2.0
"""roam-157: the served About logo/glyph must be real Roamux art, not a 1x1 stub.

Fails RED today: roamux_logo.png is a 1x1 placeholder and the SVGs do not exist yet. Goes GREEN once
roamux_logo.svg (tile) and roamux_glyph.svg (transparent glyph) ship. Runs in tier-1 (pure Python), so
the stub regression that this issue reports is caught in seconds on every PR.
"""

import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import check_settings_logo as c

_ASSETS = (pathlib.Path(__file__).resolve().parents[1].parent
           / "app" / "resources" / "settings_about")

_TILE = """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512" role="img">
  <filter id="s"><feDropShadow dx="0" dy="2" stdDeviation="7"/></filter>
  <rect x="8" y="8" width="496" height="496" rx="112" fill="#FFFFFF" filter="url(#s)"/>
  <g fill="none" stroke-linecap="round" stroke-width="90.608">
    <line x1="219" y1="219" x2="107" y2="107" stroke="#EA4335"/>
    <line x1="292" y1="219" x2="404" y2="107" stroke="#FBBC05"/>
    <line x1="292" y1="292" x2="404" y2="404" stroke="#34A853"/>
    <line x1="219" y1="292" x2="107" y2="404" stroke="#4285F4"/>
  </g>
  <circle cx="256" cy="256" r="72" fill="#FFFFFF"/>
</svg>"""

_GLYPH = """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512" role="img">
  <mask id="hole" maskUnits="userSpaceOnUse" x="0" y="0" width="512" height="512" mask-type="luminance">
    <rect x="0" y="0" width="512" height="512" fill="#FFFFFF"/>
    <circle cx="256" cy="256" r="72" fill="#000000"/>
  </mask>
  <g mask="url(#hole)" fill="none" stroke-linecap="round" stroke-width="90.608">
    <line x1="219" y1="219" x2="107" y2="107" stroke="#EA4335"/>
    <line x1="292" y1="219" x2="404" y2="107" stroke="#FBBC05"/>
    <line x1="292" y1="292" x2="404" y2="404" stroke="#34A853"/>
    <line x1="219" y1="292" x2="107" y2="404" stroke="#4285F4"/>
  </g>
</svg>"""


class ShippedAssetsTest(unittest.TestCase):
    """The real invariant: the assets in the tree pass. RED until the SVGs replace the stub."""

    def test_shipped_settings_logo_and_glyph_are_real_art(self):
        errors = c.check(_ASSETS)
        self.assertEqual([], errors, f"served About assets are not real Roamux SVG art: {errors}")


class StructuralTest(unittest.TestCase):
    """Pin what the checker actually rejects, on synthetic inputs (no dependence on the tree)."""

    def _dir_with(self, logo_bytes=None, glyph_bytes=None):
        d = pathlib.Path(tempfile.mkdtemp())
        if logo_bytes is not None:
            (d / c.LOGO).write_bytes(logo_bytes)
        if glyph_bytes is not None:
            (d / c.GLYPH).write_bytes(glyph_bytes)
        return d

    def test_valid_pair_passes(self):
        d = self._dir_with(_TILE.encode(), _GLYPH.encode())
        self.assertEqual([], c.check(d))

    def test_one_by_one_png_stub_fails(self):
        # The exact regression roam-157 reports: a 1x1 PNG where an SVG should be.
        png_1x1 = bytes.fromhex(
            "89504e470d0a1a0a0000000d494844520000000100000001080600000"
            "01f15c4890000000d49444154789c6360000002000154a24f9f0000000049454e44ae426082")
        errors = c.check(self._dir_with(png_1x1, _GLYPH.encode()))
        self.assertTrue(any(c.LOGO in e for e in errors), errors)

    def test_missing_asset_fails(self):
        errors = c.check(self._dir_with(_TILE.encode(), None))
        self.assertTrue(any(c.GLYPH in e and "missing" in e for e in errors), errors)

    def test_colourless_svg_fails(self):
        blank = b'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512"></svg>'
        errors = c.check(self._dir_with(blank, blank))
        self.assertTrue(errors)

    def test_glyph_with_white_tile_is_rejected(self):
        # A glyph that kept the tile would be near-invisible on a light nav — the whole point.
        tile_glyph = _GLYPH.replace("<mask", '<rect fill="#FFFFFF" width="512" height="512"/><mask')
        errors = c.check(self._dir_with(_TILE.encode(), tile_glyph.encode()))
        self.assertTrue(any(c.GLYPH in e and "tile" in e for e in errors), errors)

    def test_glyph_without_mask_is_rejected(self):
        no_mask = _GLYPH.replace('mask="url(#hole)"', "").replace("<mask", "<x").replace("</mask>", "</x>")
        errors = c.check(self._dir_with(_TILE.encode(), no_mask.encode()))
        self.assertTrue(any(c.GLYPH in e for e in errors), errors)


if __name__ == "__main__":
    unittest.main()

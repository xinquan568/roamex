# SPDX-License-Identifier: Apache-2.0
"""roam-158: the current-channel-logo per-scale PNGs must be the real Roamux glyph.

Fails RED today: roamux/app/resources/theme/default_{100,200}_percent/product_logo_32.png do not
exist yet. Goes GREEN once the rendered glyph PNGs ship (headless-Chrome rasterization; see the
check_toolbar_logo.py docstring for the recipe — qlmanage flattens the transparency). Runs in
tier-1 (pure Python), so a 1x1/opaque/flat stub regression is caught in seconds on every PR.
"""

import pathlib
import struct
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import check_toolbar_logo as c

_THEME = (pathlib.Path(__file__).resolve().parents[1].parent
          / "app" / "resources" / "theme")

# The four Roamux ray colours (the settings-logo palette).
_INKS = ((0x42, 0x85, 0xF4, 0xFF), (0xEA, 0x43, 0x35, 0xFF),
         (0xFB, 0xBC, 0x05, 0xFF), (0x34, 0xA8, 0x53, 0xFF))
_CLEAR = (0, 0, 0, 0)


def _png(rows):
    """Minimal 8-bit RGBA PNG from rows of (r, g, b, a) tuples."""
    height = len(rows)
    width = len(rows[0])

    def chunk(ctype, body):
        return (struct.pack(">I", len(body)) + ctype + body
                + struct.pack(">I", zlib.crc32(ctype + body)))

    raw = b"".join(
        b"\x00" + b"".join(bytes(px) for px in row) for row in rows)
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def _glyph_rows(px, inks=_INKS):
    """A synthetic but structurally valid glyph: transparent field, four ink
    quadrant strips covering well over the checker's floors."""
    rows = []
    band = px // 4
    for y in range(px):
        row = []
        for x in range(px):
            if band <= y < 2 * band and 0 <= x < px // 2:
                row.append(inks[0] if x < px // 4 else inks[1])
            elif 2 * band <= y < 3 * band and px // 2 <= x < px:
                row.append(inks[2] if x < 3 * px // 4 else inks[3])
            else:
                row.append(_CLEAR)
        rows.append(row)
    return rows


def _write_pair(theme_dir, png_100, png_200):
    for subdir, blob in (("default_100_percent", png_100),
                         ("default_200_percent", png_200)):
        target = pathlib.Path(theme_dir) / subdir / c.FILENAME
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(blob)


class ShippedAssetsTest(unittest.TestCase):
    def test_shipped_assets_pass(self):
        """The committed per-scale glyph PNGs satisfy the gate (RED until they ship)."""
        self.assertEqual([], c.check_theme_dir(_THEME))


class StubRejectionTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.theme_dir = pathlib.Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    def _failures(self):
        return c.check_theme_dir(self.theme_dir)

    def test_missing_files_fail(self):
        failures = self._failures()
        self.assertEqual(2, len(failures))
        for failure in failures:
            self.assertIn("missing", failure)

    def test_valid_synthetic_pair_passes(self):
        """The synthetic fixtures satisfy the gate — the stub cases below fail
        on their defect, not on fixture quality."""
        _write_pair(self.theme_dir, _png(_glyph_rows(32)), _png(_glyph_rows(64)))
        self.assertEqual([], self._failures())

    def test_1x1_stub_fails(self):
        stub = _png([[(255, 255, 255, 255)]])
        _write_pair(self.theme_dir, stub, stub)
        failures = self._failures()
        self.assertTrue(any("expected 32x32" in f for f in failures))
        self.assertTrue(any("expected 64x64" in f for f in failures))

    def test_opaque_background_fails(self):
        opaque = _png([[(255, 255, 255, 255)] * 32 for _ in range(32)])
        _write_pair(self.theme_dir, opaque, _png(_glyph_rows(64)))
        self.assertTrue(
            any("transparent field" in f for f in self._failures()))

    def test_flat_single_colour_fails(self):
        flat = _png(_glyph_rows(32, inks=(_INKS[0],) * 4))
        _write_pair(self.theme_dir, flat, _png(_glyph_rows(64)))
        self.assertTrue(
            any("distinct ink" in f for f in self._failures()))

    def test_wrong_scale_size_fails(self):
        """A 32px image in the 200_percent slot is a mis-placed asset."""
        _write_pair(self.theme_dir, _png(_glyph_rows(32)), _png(_glyph_rows(32)))
        self.assertTrue(
            any("expected 64x64" in f for f in self._failures()))

    def test_non_png_fails(self):
        _write_pair(self.theme_dir, b"not a png", b"not a png")
        self.assertTrue(any("bad magic" in f for f in self._failures()))


if __name__ == "__main__":
    unittest.main()

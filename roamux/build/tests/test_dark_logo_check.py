# SPDX-License-Identifier: Apache-2.0
"""roam-169: the dark-logo payload must be the real Roamux glyph and patch 0038 must stay in sync.

Fails RED today: the payload exists (T1 ships it as the expected-value fixture) but
patches/0038-dark-logo-roamux.patch does not — the shipped-artifacts case names the missing fix,
not a missing fixture (step-6 F1). Goes GREEN when 0038 ships in sync. Tier-1, pure Python.
"""

import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import check_dark_logo as c

_OVERLAY = pathlib.Path(__file__).resolve().parents[1].parent
_PAYLOAD = _OVERLAY / "app" / "resources" / "theme" / "chrome_logo_dark.svg"
_PATCH = _OVERLAY / "patches" / "0038-dark-logo-roamux.patch"


def _patch_text(svg_added_lines, version_added=None, version_removed=None,
                version_context=None):
    """A minimal synthetic 0038-shaped unified diff."""
    version_added = version_added if version_added is not None else [
        '          <img alt="$i18n{logo_alt_text}"',
        '              srcset="chrome://theme/current-channel-logo@1x 1x,',
        '                      chrome://theme/current-channel-logo@2x 2x">',
    ]
    version_removed = version_removed if version_removed is not None else [
        '          <source srcset="chrome://theme/IDR_PRODUCT_LOGO_WHITE,',
        '                          chrome://theme/IDR_PRODUCT_LOGO_WHITE@2x 2x"',
        '          <img alt="$i18n{logo_alt_text}" src="chrome://theme/IDR_PRODUCT_LOGO">',
    ]
    version_context = version_context or []
    lines = ["diff --git a/%s b/%s" % (c.UPSTREAM_SVG_PATH, c.UPSTREAM_SVG_PATH),
             "@@ -1,1 +1,%d @@" % len(svg_added_lines)]
    lines += ["-<svg>old chromium art</svg>"]
    lines += ["+" + line for line in svg_added_lines]
    lines += ["diff --git a/%s b/%s" % (c.UPSTREAM_VERSION_PATH, c.UPSTREAM_VERSION_PATH),
              "@@ -30,10 +30,5 @@"]
    lines += ["-" + line for line in version_removed]
    lines += ["+" + line for line in version_added]
    lines += [" " + line for line in version_context]
    return "\n".join(lines) + "\n"


class ShippedArtifactsTest(unittest.TestCase):
    def test_payload_is_valid_glyph(self):
        """The committed payload passes structural checks (green from T1 on)."""
        self.assertEqual([], c.check_payload(_PAYLOAD))

    def test_shipped_artifacts_pass(self):
        """Payload + patch 0038 in sync (RED until 0038 ships)."""
        self.assertEqual([], c.check_all(_PAYLOAD, _PATCH))


class PayloadRejectionTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = pathlib.Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    def _write(self, text):
        p = self.dir / "payload.svg"
        p.write_text(text)
        return p

    def test_missing_payload_fails(self):
        self.assertTrue(
            any("missing" in f for f in c.check_payload(self.dir / "nope.svg")))

    def test_flat_single_colour_fails(self):
        svg = _PAYLOAD.read_text().replace("#EA4335", "#4285F4") \
                                  .replace("#FBBC05", "#4285F4") \
                                  .replace("#34A853", "#4285F4")
        self.assertTrue(
            any("palette" in f for f in c.check_payload(self._write(svg))))

    def test_tile_rect_fails(self):
        svg = _PAYLOAD.read_text().replace(
            "</svg>", '<rect x="8" y="8" width="496" height="496" fill="#FFF"/></svg>')
        self.assertTrue(
            any("tile" in f for f in c.check_payload(self._write(svg))))

    def test_drop_shadow_fails(self):
        svg = _PAYLOAD.read_text().replace(
            "</svg>", '<filter id="s"><feDropShadow dx="0" dy="2"/></filter></svg>')
        self.assertTrue(
            any("drop-shadow" in f for f in c.check_payload(self._write(svg))))

    def test_wrong_intrinsic_size_fails(self):
        svg = _PAYLOAD.read_text().replace('width="24" height="24"',
                                           'width="512" height="512"')
        self.assertTrue(
            any("width/height" in f for f in c.check_payload(self._write(svg))))

    def test_missing_mask_fails(self):
        text = _PAYLOAD.read_text()
        start = text.index("<mask")
        end = text.index("</mask>") + len("</mask>")
        self.assertTrue(any("mask" in f for f in
                            c.check_payload(self._write(text[:start] + text[end:]))))


class PatchSyncTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = pathlib.Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    def _patch(self, text):
        p = self.dir / "0038.patch"
        p.write_text(text)
        return p

    def test_missing_patch_fails(self):
        failures = c.check_patch_sync(_PAYLOAD, self.dir / "nope.patch")
        self.assertTrue(any("missing" in f for f in failures))

    def test_synced_patch_passes(self):
        patch = self._patch(_patch_text(_PAYLOAD.read_text().splitlines()))
        self.assertEqual([], c.check_patch_sync(_PAYLOAD, patch))

    def test_desynced_svg_fails(self):
        lines = _PAYLOAD.read_text().replace("#EA4335", "#000000").splitlines()
        patch = self._patch(_patch_text(lines))
        self.assertTrue(any("differ from the committed payload" in f
                            for f in c.check_patch_sync(_PAYLOAD, patch)))

    def test_lingering_idr_reference_fails(self):
        patch = self._patch(_patch_text(
            _PAYLOAD.read_text().splitlines(),
            version_added=['<img src="chrome://theme/IDR_PRODUCT_LOGO">']))
        self.assertTrue(any("leaves an IDR_PRODUCT_LOGO" in f
                            for f in c.check_patch_sync(_PAYLOAD, patch)))

    def test_context_idr_reference_fails(self):
        """An IDR reference surviving as unchanged hunk context must fail —
        added lines alone do not tell the whole post-patch story."""
        patch = self._patch(_patch_text(
            _PAYLOAD.read_text().splitlines(),
            version_context=[
                '<img src="chrome://theme/IDR_PRODUCT_LOGO" id="legacy">']))
        self.assertTrue(any("retained as hunk context" in f
                            for f in c.check_patch_sync(_PAYLOAD, patch)))

    def test_missing_current_channel_srcset_fails(self):
        patch = self._patch(_patch_text(
            _PAYLOAD.read_text().splitlines(),
            version_added=['<img alt="x" src="images/other.png">']))
        self.assertTrue(any("current-channel-logo" in f
                            for f in c.check_patch_sync(_PAYLOAD, patch)))


if __name__ == "__main__":
    unittest.main()

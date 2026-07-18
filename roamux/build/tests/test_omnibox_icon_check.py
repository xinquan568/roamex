# SPDX-License-Identifier: Apache-2.0
"""roam-159: the omnibox-chip Roamux icon must be real fixed-colour art, patch 0039 surgical.

Fails RED today: the .icon ships in T1 (fixture-first) but
patches/0039-omnibox-chip-roamux-icon.patch does not exist yet — the shipped-artifacts case names
the missing fix. Goes GREEN when 0039 ships surgical. Tier-1, pure Python.
"""

import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import check_omnibox_icon as c

_OVERLAY = pathlib.Path(__file__).resolve().parents[1].parent
_ICON = _OVERLAY / c.ICON_REL
_PATCH = _OVERLAY / c.PATCH_REL


def _patch_text(delegate_added=None, delegate_removed=None, extra_file=None,
                gn_added=None, delegate_context=None, gn_header=None,
                gn_context=None):
    delegate_added = delegate_added if delegate_added is not None else [
        '    return &roamux::kRoamuxProductIcon;',
    ]
    delegate_removed = delegate_removed if delegate_removed is not None else [
        '    return &omnibox::kProductChromeRefreshIcon;',
    ]
    delegate_context = delegate_context if delegate_context is not None else [
        '  if (url.SchemeIs(content::kChromeUIScheme)) {',
    ]
    gn_added = gn_added if gn_added is not None else [
        '    "//roamux/browser/ui/icons",',
    ]
    gn_header = gn_header if gn_header is not None else \
        'source_set("toolbar") {'
    gn_context = gn_context if gn_context is not None else [
        '      "//components/user_education/common",',
    ]
    lines = ["diff --git a/%s b/%s" % (c.DELEGATE_PATH, c.DELEGATE_PATH),
             "@@ -190,7 +191,9 @@ const gfx::VectorIcon* "
             "ChromeLocationBarModelDelegate::GetVectorIconOverride()"]
    lines += [" " + l for l in delegate_context]
    lines += ["-" + l for l in delegate_removed]
    lines += ["+" + l for l in delegate_added]
    lines += ["diff --git a/%s b/%s" % (c.TOOLBAR_GN_PATH, c.TOOLBAR_GN_PATH),
              "@@ -77,6 +77,7 @@ " + gn_header]
    lines += [" " + l for l in gn_context]
    lines += ["+" + l for l in gn_added]
    if extra_file:
        lines += ["diff --git a/%s b/%s" % (extra_file, extra_file),
                  "@@ -1,1 +1,1 @@", "-x", "+y"]
    return "\n".join(lines) + "\n"


class ShippedArtifactsTest(unittest.TestCase):
    def test_icon_is_valid(self):
        """The committed .icon passes structural checks (green from T1 on)."""
        self.assertEqual([], c.check_icon(_ICON))

    def test_shipped_artifacts_pass(self):
        """Icon + patch 0039 surgical (RED until 0039 ships)."""
        self.assertEqual([], c.check_all(_ICON, _PATCH))


class IconRejectionTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = pathlib.Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    def _write(self, text):
        p = self.dir / "icon.icon"
        p.write_text(text)
        return p

    def test_missing_icon_fails(self):
        self.assertTrue(any("missing" in f
                            for f in c.check_icon(self.dir / "nope.icon")))

    def test_tint_driven_icon_fails(self):
        """An icon without the fixed palette (i.e. tinted at paint time) fails."""
        text = _ICON.read_text().replace("PATH_COLOR_ARGB", "IGNORED")
        self.assertTrue(any("palette" in f or "colour" in f
                            for f in c.check_icon(self._write(text))))

    def test_square_caps_fail(self):
        text = _ICON.read_text().replace("STROKE, 2.83f,",
                                         "STROKE, 2.83f,\nCAP_SQUARE,", 1)
        self.assertTrue(any("CAP_SQUARE" in f
                            for f in c.check_icon(self._write(text))))

    def test_too_few_rays_fail(self):
        text = _ICON.read_text().replace("STROKE,", "IGNORE,", 2)
        self.assertTrue(any("expected >= 4 rays" in f
                            for f in c.check_icon(self._write(text))))

    def test_extra_fixed_colour_fails(self):
        """A fifth PATH_COLOR_ARGB row (off-palette fixed colour) fails the
        exact-palette check."""
        text = _ICON.read_text().replace(
            "</svg>" if "</svg>" in _ICON.read_text() else "LINE_TO, 3.36f, 12.64f",
            "LINE_TO, 3.36f, 12.64f,\nNEW_PATH,\n"
            "PATH_COLOR_ARGB, 0xFF, 0x00, 0x00, 0x00,\n"
            "STROKE, 1.0f,\nMOVE_TO, 1, 1,\nLINE_TO, 2, 2")
        self.assertTrue(any("exactly" in f
                            for f in c.check_icon(self._write(text))))


class PatchSurgicalityTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = pathlib.Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    def _patch(self, text):
        p = self.dir / "0039.patch"
        p.write_text(text)
        return p

    def test_missing_patch_fails(self):
        self.assertTrue(any("missing" in f
                            for f in c.check_patch(self.dir / "no.patch")))

    def test_surgical_patch_passes(self):
        self.assertEqual([], c.check_patch(self._patch(_patch_text())))

    def test_extra_file_fails(self):
        patch = self._patch(_patch_text(
            extra_file="components/omnibox/browser/vector_icons/product_chrome_refresh.icon"))
        self.assertTrue(any("beyond the declared surface" in f
                            for f in c.check_patch(patch)))

    def test_missing_roamux_return_fails(self):
        patch = self._patch(_patch_text(
            delegate_added=['    return &omnibox::kSomethingElse;']))
        self.assertTrue(any("does not return the Roamux icon" in f
                            for f in c.check_patch(patch)))

    def test_touching_other_arm_fails(self):
        patch = self._patch(_patch_text(
            delegate_removed=[
                '    return &omnibox::kProductChromeRefreshIcon;',
                '    return &vector_icons::kGoogleColorIcon;']))
        self.assertTrue(any("kGoogleColorIcon" in f
                            for f in c.check_patch(patch)))

    def test_missing_dep_edge_fails(self):
        patch = self._patch(_patch_text(gn_added=['    "//other/dep",']))
        self.assertTrue(any("dep edge" in f for f in c.check_patch(patch)))

    def test_wrong_arm_anchor_fails(self):
        """A swap landing in a hunk NOT framed by the kChromeUIScheme test
        (e.g. the extension arm) must fail the anchor check."""
        patch = self._patch(_patch_text(
            delegate_context=['  if (url.SchemeIs(extensions::kExtensionScheme)) {']))
        self.assertTrue(any("not anchored in the kChromeUIScheme arm" in f
                            for f in c.check_patch(patch)))

    def test_wrong_target_dep_fails(self):
        patch = self._patch(_patch_text(gn_header='source_set("impl") {'))
        self.assertTrue(any('not anchored in source_set("toolbar")' in f
                            for f in c.check_patch(patch)))

    def test_wrong_scope_dep_fails(self):
        """A dep inserted outside the non-Android deps block (different
        sibling context) must fail the scope check."""
        patch = self._patch(_patch_text(
            gn_context=['    "//components/search",']))
        self.assertTrue(any("wrong scope" in f for f in c.check_patch(patch)))


if __name__ == "__main__":
    unittest.main()

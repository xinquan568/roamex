# SPDX-License-Identifier: Apache-2.0
"""roam-141: tag → Sparkle-orderable CFBundleVersion mapping.

Pins the encoding AND the ordering property that actually matters — the encoded
versions must sort the way releases progress, because Sparkle's comparator only
understands numeric dotted versions.
"""

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import release_version as rv


class BundleVersionTest(unittest.TestCase):
    def test_alpha_encoding(self):
        self.assertEqual(rv.bundle_version("v0.0.1-alpha.3"), "0.0.1.1.3")

    def test_leading_v_optional(self):
        self.assertEqual(rv.bundle_version("0.0.1-alpha.3"), "0.0.1.1.3")

    def test_stages_map_to_distinct_numbers(self):
        self.assertEqual(rv.bundle_version("v1.2.3-beta.4"), "1.2.3.2.4")
        self.assertEqual(rv.bundle_version("v1.2.3-rc.4"), "1.2.3.3.4")

    def test_final_release_sorts_above_prereleases(self):
        self.assertEqual(rv.bundle_version("v0.0.1"), "0.0.1.9.0")

    def test_bad_tag_rejected(self):
        for bad in ["1.0", "v0.0.1-gamma.1", "v0.0.1-alpha", "nope"]:
            with self.assertRaises(ValueError, msg=bad):
                rv.bundle_version(bad)

    def test_short_version_is_human(self):
        self.assertEqual(rv.short_version("v0.0.1-alpha.3"), "0.0.1-alpha.3")
        self.assertEqual(rv.short_version("v0.0.1"), "0.0.1")


def _key(bundle):
    return [int(p) for p in bundle.split(".")]


class OrderingPropertyTest(unittest.TestCase):
    # The releases in ascending order; encoded keys must be strictly increasing
    # (this is exactly what Sparkle's numeric comparator will see).
    ASCENDING = [
        "v0.0.1-alpha.2", "v0.0.1-alpha.3", "v0.0.1-alpha.9", "v0.0.1-alpha.10",
        "v0.0.1-beta.1", "v0.0.1-rc.1", "v0.0.1", "v0.0.2-alpha.1", "v0.1.0-alpha.1",
    ]

    def test_encoded_keys_are_strictly_increasing(self):
        keys = [_key(rv.bundle_version(t)) for t in self.ASCENDING]
        for a, b, ta, tb in zip(keys, keys[1:], self.ASCENDING, self.ASCENDING[1:]):
            self.assertLess(a, b, f"{ta} ({a}) must sort below {tb} ({b})")


class SourceVersionTest(unittest.TestCase):
    """roam-156: roamux/build/VERSION is the single source of the marketing version.

    Before this, the marketing string existed only in the git tag and the appcast, so
    nothing in the running binary knew it was 0.0.1-alpha.6 — the About page fell back
    to the Chromium version. VERSION inverts that: the file is the source, the tag is
    validated against it.
    """

    def test_read_source_version_matches_version_file(self):
        raw = (pathlib.Path(rv.__file__).resolve().parent / "VERSION").read_text()
        self.assertEqual(rv.read_source_version(),
                         raw.split("=", 1)[1].strip())

    def test_version_file_parses_under_chromium_version_py(self):
        # //build/util/version.py FetchValuesFromFile does
        #   line.rstrip('\r\n').split('=', 1)
        # on EVERY line, with no comment or blank-line handling — a '#' SPDX header or
        # a blank line raises ValueError and breaks the GN build. Pin the shape here,
        # where it costs milliseconds, rather than discovering it as a confusing gn error.
        path = pathlib.Path(rv.__file__).resolve().parent / "VERSION"
        lines = path.read_text().split("\n")
        self.assertEqual(lines[-1], "", "VERSION must end with exactly one newline")
        for line in lines[:-1]:
            self.assertIn("=", line, f"non-KEY=VALUE line would ValueError: {line!r}")
            self.assertFalse(line.startswith("#"), f"comment breaks the parser: {line!r}")
            self.assertNotEqual(line.strip(), "", "blank line breaks the parser")


class SourceTagAgreementTest(unittest.TestCase):
    """The invariant roam-156 exists to protect: the two producers of the marketing
    string must never disagree, or the About page and the Sparkle update dialog tell
    the user different things about the same binary."""

    def test_source_version_round_trips_through_tag_grammar(self):
        # The stored string must be one _TAG_RE accepts, so the compiled-in value and
        # the appcast's shortVersionString are the same string by construction.
        #
        # Assert via bundle_version(), NOT short_version(): short_version only strips a
        # leading 'v' and never parses, so comparing short_version("v"+src) to src would
        # be a tautology about string slicing and would pass for any src whatsoever.
        src = rv.read_source_version()
        rv.bundle_version("v" + src)  # raises if the stored string is not in the grammar

    def test_check_tag_rejects_a_matching_but_ungrammatical_pair(self):
        # The hole a naive check_tag leaves: tag and VERSION agree as strings, but the
        # version is not one the encoder understands ('gamma' is not a stage). Without
        # parsing, this clears the early gate and then dies in bundle_version() later in
        # the release job — after the build, which is exactly what the gate must prevent.
        self.assertFalse(
            rv.check_tag("v0.0.1-gamma.1", source_version="0.0.1-gamma.1"))
        self.assertFalse(rv.check_tag("not-a-version", source_version="not-a-version"))
        # And anything check_tag accepts must actually encode.
        self.assertTrue(rv.check_tag("v0.0.2", source_version="0.0.2"))
        rv.bundle_version("v0.0.2")

    def test_check_tag_accepts_matching_tag(self):
        src = rv.read_source_version()
        self.assertTrue(rv.check_tag("v" + src))
        self.assertTrue(rv.check_tag(src))  # leading v optional, as _TAG_RE allows

    def test_check_tag_rejects_mismatched_tag(self):
        self.assertFalse(rv.check_tag("v9.9.9-alpha.1"))

    def test_check_tag_handles_final_release_with_no_stage_segment(self):
        # A final release is '0.0.2', NOT '0.0.2-final.0' — _TAG_RE's pre-release group
        # is optional. This is the case a decomposed MAJOR/MINOR/PATCH/STAGE/N VERSION
        # file could not round-trip, and is why the marketing string is stored verbatim.
        self.assertTrue(rv.check_tag("v0.0.2", source_version="0.0.2"))
        self.assertFalse(rv.check_tag("v0.0.2", source_version="0.0.2-alpha.1"))

    def test_field_source_is_what_the_appcast_consumes(self):
        # Guards T3b: release.yml's appcast shortVersionString must stay file-derived.
        # If someone repoints it at --field short --tag, this and the workflow diverge.
        self.assertEqual(rv.read_source_version(), rv.field_value("source", tag=None))


if __name__ == "__main__":
    unittest.main()

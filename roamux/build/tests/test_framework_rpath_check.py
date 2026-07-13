# SPDX-License-Identifier: Apache-2.0
"""roam-121: launch-critical dyld resolution check for the app's main framework.

The stub executable dlopen()s the Chromium framework; if any of the framework's
@rpath/ dependencies cannot be resolved through the framework's own LC_RPATHs to a
real file inside the bundle, dyld refuses the load and the installed app aborts at
first launch ("no LC_RPATH's found"). These tests pin check_framework_rpath.py's
otool parser and resolver hermetically: fixture otool -l output, tmpdir bundle
layouts, no real binaries.
"""

import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import check_framework_rpath as cfr

OTOOL_NO_RPATH = """\
/x/Chromium Framework:
Load command 10
          cmd LC_LOAD_DYLIB
      cmdsize 96
         name @rpath/Sparkle.framework/Versions/B/Sparkle (offset 24)
Load command 11
          cmd LC_LOAD_DYLIB
      cmdsize 56
         name /usr/lib/libSystem.B.dylib (offset 24)
"""

OTOOL_WITH_RPATH = OTOOL_NO_RPATH + """\
Load command 12
          cmd LC_RPATH
      cmdsize 40
         path @loader_path/../../.. (offset 12)
"""


class ParseLoadCommandsTest(unittest.TestCase):
    def test_rpath_deps_extracted(self):
        rpaths, deps = cfr.parse_load_commands(OTOOL_NO_RPATH)
        self.assertEqual(deps, ["@rpath/Sparkle.framework/Versions/B/Sparkle"],
                         "only @rpath/ deps are the check's subject")
        self.assertEqual(rpaths, [], "fixture carries no LC_RPATH")

    def test_lc_rpath_extracted(self):
        rpaths, deps = cfr.parse_load_commands(OTOOL_WITH_RPATH)
        self.assertEqual(rpaths, ["@loader_path/../../.."])
        self.assertEqual(deps, ["@rpath/Sparkle.framework/Versions/B/Sparkle"])

    def test_non_rpath_deps_ignored(self):
        _, deps = cfr.parse_load_commands(OTOOL_NO_RPATH)
        self.assertNotIn("/usr/lib/libSystem.B.dylib", deps,
                         "absolute-path deps are dyld's problem, not the bundle's")


class UnresolvedDepsTest(unittest.TestCase):
    def _bundle(self, tmp, with_sparkle):
        contents = pathlib.Path(tmp) / "Roamux.app" / "Contents"
        loader_dir = (contents / "Frameworks" / "Chromium Framework.framework"
                      / "Versions" / "149.0.0.0")
        loader_dir.mkdir(parents=True)
        if with_sparkle:
            sp = contents / "Frameworks" / "Sparkle.framework" / "Versions" / "B"
            sp.mkdir(parents=True)
            (sp / "Sparkle").write_bytes(b"\xca\xfe")
        return loader_dir

    def test_no_rpaths_is_unresolved(self):
        with tempfile.TemporaryDirectory() as tmp:
            loader_dir = self._bundle(tmp, with_sparkle=True)
            problems = cfr.unresolved_deps(
                ["@rpath/Sparkle.framework/Versions/B/Sparkle"], [], loader_dir)
            self.assertEqual(len(problems), 1,
                             "a dep with zero LC_RPATHs must be reported")

    def test_loader_path_rpath_resolves(self):
        with tempfile.TemporaryDirectory() as tmp:
            loader_dir = self._bundle(tmp, with_sparkle=True)
            problems = cfr.unresolved_deps(
                ["@rpath/Sparkle.framework/Versions/B/Sparkle"],
                ["@loader_path/../../.."], loader_dir)
            self.assertEqual(problems, [],
                             "@loader_path/../../.. must reach Contents/Frameworks")

    def test_rpath_present_but_target_missing(self):
        with tempfile.TemporaryDirectory() as tmp:
            loader_dir = self._bundle(tmp, with_sparkle=False)
            problems = cfr.unresolved_deps(
                ["@rpath/Sparkle.framework/Versions/B/Sparkle"],
                ["@loader_path/../../.."], loader_dir)
            self.assertEqual(len(problems), 1,
                             "an rpath that resolves to a missing file is still broken")


if __name__ == "__main__":
    unittest.main()

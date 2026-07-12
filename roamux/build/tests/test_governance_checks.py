# SPDX-License-Identifier: Apache-2.0
"""Hermetic tests for the governance checkers (roam-38) — the single source of truth the hooks AND CI
both invoke. No Chromium checkout, no external deps.
"""

import pathlib
import subprocess
import sys
import tempfile
import unittest

CHECKS = pathlib.Path(__file__).resolve().parents[3] / "scripts" / "checks"


def run_check(script, *args, stdin=None, cwd=None):
    return subprocess.run([sys.executable, str(CHECKS / script), *args],
                          capture_output=True, text=True, input=stdin, cwd=cwd)


class TmpTree(unittest.TestCase):
    def setUp(self):
        self.tmp = pathlib.Path(tempfile.mkdtemp(prefix="roamux-gov-"))
        self.addCleanup(__import__("shutil").rmtree, self.tmp, ignore_errors=True)

    def write(self, rel, text):
        f = self.tmp / rel
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_text(text)
        return str(f)


class SpdxHeaderTest(TmpTree):
    def test_missing_header_rejected(self):
        f = self.write("roamux/common/x.cc", "int main() { return 0; }\n")
        r = run_check("spdx_header.py", f)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("x.cc", r.stdout + r.stderr)

    def test_present_header_accepted(self):
        f = self.write("roamux/common/x.cc", "// SPDX-License-Identifier: Apache-2.0\nint main(){}\n")
        self.assertEqual(run_check("spdx_header.py", f).returncode, 0)

    def test_chromium_src_copy_exempt(self):
        # A full upstream copy keeps the BSD header and must NOT be required to carry ours.
        f = self.write("roamux/chromium_src/chrome/x.h", "// Copyright The Chromium Authors\n")
        self.assertEqual(run_check("spdx_header.py", f).returncode, 0)

    def test_markdown_doc_exempt(self):
        f = self.write("README.md", "# Roamux\nno header here\n")
        self.assertEqual(run_check("spdx_header.py", f).returncode, 0)

    def test_json_data_exempt(self):
        f = self.write("roamux/build/override_signatures.json", '{"pin":"x"}\n')
        self.assertEqual(run_check("spdx_header.py", f).returncode, 0)


class SecretScanTest(TmpTree):
    def test_aws_key_rejected(self):
        # A realistic key (not the documented ...EXAMPLE fake, which is legitimately allowlisted).
        f = self.write("x.py", "KEY = 'AKIAZ7XQ2WPL4NR8YT3D'\n")  # roamux:allow-secret (fixture)
        r = run_check("secret_scan.py", f)
        self.assertNotEqual(r.returncode, 0)

    def test_documented_example_key_allowlisted(self):
        # The AWS-docs fake key must NOT trip the scanner.
        f = self.write("x.py", "KEY = 'AKIAIOSFODNN7EXAMPLE'\n")  # roamux:allow-secret (fixture)
        self.assertEqual(run_check("secret_scan.py", f).returncode, 0)

    def test_pem_private_key_rejected(self):
        f = self.write("x.pem", "-----BEGIN OPENSSH PRIVATE KEY-----\nabc\n")  # roamux:allow-secret (fixture)
        self.assertNotEqual(run_check("secret_scan.py", f).returncode, 0)

    def test_keys_template_placeholder_allowlisted(self):
        f = self.write("roamux/build/google_keys.gni.template",
                       '# google_api_key = "<your key>"\n')
        self.assertEqual(run_check("secret_scan.py", f).returncode, 0)


    def test_allow_marker_honored_in_test_path(self):
        f = self.write("roamux/build/tests/test_x.py",
                       "KEY = 'AKIAZ7XQ2WPL4NR8YT3D'  # roamux:allow-secret\n")
        self.assertEqual(run_check("secret_scan.py", f).returncode, 0)

    def test_allow_marker_ignored_in_normal_source(self):
        # A normal source/config file cannot silence detection with the marker (abuse surface closed).
        f = self.write("roamux/common/x.cc",
                       "const char* K = \"AKIAZ7XQ2WPL4NR8YT3D\";  // roamux:allow-secret\n")
        self.assertNotEqual(run_check("secret_scan.py", f).returncode, 0)

    def test_allow_marker_ignored_when_path_merely_contains_test_(self):
        # 'test_' as a bare substring of a non-test path must NOT enable suppression.
        f = self.write("roamux/browser/contest_manager.cc",
                       "K = 'AKIAZ7XQ2WPL4NR8YT3D'  # roamux:allow-secret\n")
        r = run_check("secret_scan.py", f, cwd=str(self.tmp))
        # invoke with the repo-relative path so the dir/basename gate sees the real shape
        r2 = subprocess.run([sys.executable, str(CHECKS / "secret_scan.py"),
                             "roamux/browser/contest_manager.cc"],
                            capture_output=True, text=True, cwd=str(self.tmp))
        self.assertNotEqual(r2.returncode, 0)


class CommitMsgTest(TmpTree):
    def test_conventional_accepted(self):
        f = self.write("m", "feat(prefs): add the thing\n")
        self.assertEqual(run_check("commit_msg.py", f).returncode, 0)

    def test_valid_subject_without_roam_n_accepted(self):
        # roam-38 checks SYNTAX ONLY — no roam-N linkage (that is roam-39's gate).
        f = self.write("m", "chore: tidy up\n")
        self.assertEqual(run_check("commit_msg.py", f).returncode, 0)

    def test_non_conventional_rejected(self):
        for bad in ("wip\n", "updated stuff\n", "Fixed the bug\n"):
            f = self.write("m", bad)
            self.assertNotEqual(run_check("commit_msg.py", f).returncode, 0, bad)


class PrePushDecisionTest(unittest.TestCase):
    def _decision(self, environ):
        import importlib.util
        spec = importlib.util.spec_from_file_location(
            "pre_push", str(CHECKS / "pre_push.py"))
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        return mod.gtest_decision(environ)

    def test_skips_when_unset(self):
        self.assertEqual(self._decision({})[0], "skip")

    def test_skips_when_no_out_dir(self):
        self.assertEqual(self._decision({"ROAMEX_CHROMIUM_SRC": "/nonexistent"})[0], "skip")

    def test_runs_when_out_default_present(self):
        d = pathlib.Path(tempfile.mkdtemp(prefix="roamux-fakesrc-"))
        self.addCleanup(__import__("shutil").rmtree, d, ignore_errors=True)
        (d / "out" / "Default").mkdir(parents=True)
        action, src = self._decision({"ROAMEX_CHROMIUM_SRC": str(d)})
        self.assertEqual(action, "run")
        self.assertEqual(src, str(d))


class OverlayStructureTest(TmpTree):
    def check_rel(self, rel):
        self.write(rel, "// SPDX-License-Identifier: Apache-2.0\nx\n" if rel.endswith((".cc", ".h"))
                   else "diff\n")
        return run_check("overlay_structure.py", rel, cwd=str(self.tmp))

    def test_bad_patch_name_rejected(self):
        self.assertNotEqual(self.check_rel("roamux/patches/bad name.patch").returncode, 0)

    def test_good_patch_name_accepted(self):
        self.assertEqual(self.check_rel("roamux/patches/0005-ok.patch").returncode, 0)

    def test_our_file_at_upstream_path_rejected(self):
        r = self.check_rel("chrome/browser/x.cc")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("upstream mirror path", r.stdout + r.stderr)

    def test_our_file_under_chromium_src_accepted(self):
        self.assertEqual(self.check_rel("roamux/chromium_src/chrome/browser/x.h").returncode, 0)

    def test_additive_roamux_file_accepted(self):
        self.assertEqual(self.check_rel("roamux/browser/x.cc").returncode, 0)


if __name__ == "__main__":
    unittest.main()

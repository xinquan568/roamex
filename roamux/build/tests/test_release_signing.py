# SPDX-License-Identifier: Apache-2.0
"""Hermetic tests for the roam-33 release signing/packaging logic — no Apple
credentials, no real codesign (that is the release-job E2E). Covers the pure
seams: the signing-mode gate, the inside-out combined sign order, Sparkle
nested-code discovery, and packaging symlink/exec-bit preservation."""

import os
import pathlib
import stat
import subprocess
import sys
import tempfile
import unittest

REL = pathlib.Path(__file__).resolve().parent.parent.parent / "app" / "release"
sys.path.insert(0, str(REL))

import package_roamux  # noqa: E402
import rename_bundle  # noqa: E402
import signing_mode  # noqa: E402
import roamux_signing_config  # noqa: E402
import signing_plan  # noqa: E402

SECRETS = [
    "ROAMEX_DEVELOPER_ID_CERT_P12",
    "ROAMEX_DEVELOPER_ID_CERT_PASSWORD",
    "ROAMEX_NOTARY_KEY_ID",
    "ROAMEX_NOTARY_ISSUER_ID",
    "ROAMEX_NOTARY_PRIVATE_KEY",
]


class SigningModeTest(unittest.TestCase):
    def test_all_secrets_is_signed(self):
        env = {k: "x" for k in SECRETS}
        self.assertEqual(signing_mode.resolve_signing_mode(env), "signed")

    def test_no_secrets_is_unsigned(self):
        self.assertEqual(signing_mode.resolve_signing_mode({}), "unsigned")

    def test_partial_secrets_fail_fast_naming_missing(self):
        env = {SECRETS[0]: "x", SECRETS[1]: "x"}  # cert but no notary
        with self.assertRaises(signing_mode.PartialSigningSecretsError) as ctx:
            signing_mode.resolve_signing_mode(env)
        msg = str(ctx.exception)
        self.assertIn("ROAMEX_NOTARY_KEY_ID", msg)
        self.assertNotIn("ROAMEX_DEVELOPER_ID_CERT_P12", msg)

    def test_blank_values_count_as_absent(self):
        env = {k: "" for k in SECRETS}
        self.assertEqual(signing_mode.resolve_signing_mode(env), "unsigned")


class SparkleDiscoveryTest(unittest.TestCase):
    def setUp(self):
        self.tmp = pathlib.Path(tempfile.mkdtemp(prefix="roamux-fw-"))
        self.addCleanup(_rmtree, self.tmp)
        fw = self.tmp / "Sparkle.framework" / "Versions" / "B"
        (fw / "XPCServices" / "Installer.xpc" / "Contents" / "MacOS").mkdir(
            parents=True)
        (fw / "XPCServices" / "Downloader.xpc" / "Contents" / "MacOS").mkdir(
            parents=True)
        (fw / "Updater.app" / "Contents" / "MacOS").mkdir(parents=True)
        _exe(fw / "XPCServices" / "Installer.xpc" / "Contents" / "MacOS" / "Installer")
        _exe(fw / "XPCServices" / "Downloader.xpc" / "Contents" / "MacOS" / "Downloader")
        _exe(fw / "Updater.app" / "Contents" / "MacOS" / "Updater")
        _exe(fw / "Autoupdate")
        _exe(fw / "Sparkle")
        self.framework = self.tmp / "Sparkle.framework"

    def test_discovers_all_nested_code_deepest_first(self):
        parts = signing_plan.discover_sparkle_parts(self.framework)
        names = [p.name for p in parts]
        for n in ("Installer.xpc", "Downloader.xpc", "Updater.app",
                  "Autoupdate"):
            self.assertIn(n, names, f"{n} not discovered")
        # Deepest-first: nested code precedes the framework itself.
        self.assertEqual(parts[-1].name, "Sparkle.framework")

    def test_autoupdate_must_be_planned(self):
        # A plan missing the loose Autoupdate executable is rejected.
        plan = [p for p in signing_plan.discover_sparkle_parts(self.framework)
                if p.name != "Autoupdate"]
        with self.assertRaises(signing_plan.UnplannedSparkleCodeError):
            signing_plan.assert_sparkle_fully_planned(self.framework, plan)

    def test_full_discovery_satisfies_the_plan_check(self):
        parts = signing_plan.discover_sparkle_parts(self.framework)
        signing_plan.assert_sparkle_fully_planned(self.framework, parts)

    def test_missing_nested_code_fails_the_plan_check(self):
        plan = [self.framework]  # framework only, XPC/app omitted
        with self.assertRaises(signing_plan.UnplannedSparkleCodeError):
            signing_plan.assert_sparkle_fully_planned(self.framework, plan)


class RoamuxSigningConfigTest(unittest.TestCase):
    def test_config_overrides_product_and_bundle_id(self):
        class StubBase:
            pass
        cls = roamux_signing_config.make_roamux_config_class(StubBase)
        cfg = cls()
        self.assertEqual(cfg.product, "Roamux")
        self.assertEqual(cfg.app_product, "Roamux")
        self.assertEqual(cfg.base_bundle_id, "com.roamux.Roamux")

    def test_get_parts_injects_sparkle_before_outer_app(self):
        keys = ["chromium_framework", "chromium_helper", "app"]  # app last
        ordered = roamux_signing_config.roamux_get_parts({}, keys)
        self.assertEqual(ordered[-1], "app")
        sparkle = [k for k in ordered if k.startswith("sparkle:")]
        self.assertTrue(sparkle, "no Sparkle parts injected")
        for k in sparkle:
            self.assertLess(ordered.index(k), ordered.index("app"))
        # Autoupdate + the framework are represented.
        self.assertIn("sparkle:Autoupdate", ordered)
        self.assertIn("sparkle:Sparkle.framework", ordered)

    def test_load_base_returns_none_without_chromium_checkout(self):
        # No signing package on a bare dir -> None (plan-preview fallback).
        tmp = pathlib.Path(tempfile.mkdtemp(prefix="roamux-noc-"))
        self.addCleanup(_rmtree, tmp)
        self.assertIsNone(
            roamux_signing_config.load_chromium_config_base(tmp))

    def test_config_class_reports_roamux_over_a_stub_chromium_base(self):
        class StubChromiumConfig:
            @property
            def app_product(self):
                return "Chromium"
        cls = roamux_signing_config.make_roamux_config_class(StubChromiumConfig)
        self.assertEqual(cls().app_product, "Roamux")


class CombinedOrderTest(unittest.TestCase):
    def test_outer_app_is_last_no_nested_after(self):
        chromium_parts = ["app/Contents/Frameworks/Chromium Framework.framework",
                          "app/Contents/Frameworks/Chromium Helper.app"]
        sparkle_parts = ["Sparkle.framework/Versions/B/XPCServices/Installer.xpc",
                         "Sparkle.framework"]
        outer = "app"
        plan = signing_plan.combined_sign_plan(chromium_parts, sparkle_parts,
                                               outer)
        self.assertEqual(plan[-1], outer)
        outer_idx = plan.index(outer)
        for p in chromium_parts + sparkle_parts:
            self.assertLess(plan.index(p), outer_idx,
                            f"{p} must be signed before the outer app")


class RenameBundleTest(unittest.TestCase):
    def test_chromium_app_becomes_roamux_app(self):
        import plistlib
        tmp = pathlib.Path(tempfile.mkdtemp(prefix="roamux-rn-"))
        self.addCleanup(_rmtree, tmp)
        contents = tmp / "Chromium.app" / "Contents"
        (contents / "MacOS").mkdir(parents=True)
        _exe(contents / "MacOS" / "Chromium")
        with open(contents / "Info.plist", "wb") as f:
            plistlib.dump({"CFBundleExecutable": "Chromium",
                           "CFBundleName": "Chromium",
                           "CFBundleIdentifier": "org.chromium.Chromium"}, f)
        new = rename_bundle.rename_bundle(tmp / "Chromium.app")
        self.assertEqual(new.name, "Roamux.app")
        self.assertTrue((new / "Contents" / "MacOS" / "Roamux").is_file())
        with open(new / "Contents" / "Info.plist", "rb") as f:
            plist = plistlib.load(f)
        self.assertEqual(plist["CFBundleExecutable"], "Roamux")
        self.assertEqual(plist["CFBundleName"], "Roamux")
        self.assertEqual(plist["CFBundleIdentifier"], "com.roamux.Roamux")


class PackagingTest(unittest.TestCase):
    def setUp(self):
        self.tmp = pathlib.Path(tempfile.mkdtemp(prefix="roamux-pkg-"))
        self.addCleanup(_rmtree, self.tmp)
        app = self.tmp / "Roamux.app" / "Contents"
        (app / "Frameworks" / "F.framework" / "Versions" / "B").mkdir(
            parents=True)
        os.symlink("B", app / "Frameworks" / "F.framework" / "Versions" /
                   "Current")
        _exe(app / "MacOS" / "Roamux")
        self.app = self.tmp / "Roamux.app"

    def _assert_preserved(self, extracted_app):
        cur = (extracted_app / "Contents" / "Frameworks" / "F.framework" /
               "Versions" / "Current")
        self.assertTrue(cur.is_symlink(), "framework symlink lost")
        exe = extracted_app / "Contents" / "MacOS" / "Roamux"
        self.assertTrue(os.access(exe, os.X_OK), "exec bit lost")

    def test_zip_preserves_symlinks_and_exec_bits(self):
        out = self.tmp / "Roamux.zip"
        package_roamux.package_zip(self.app, out)
        dest = self.tmp / "unzip"
        dest.mkdir()
        subprocess.run(["ditto", "-x", "-k", str(out), str(dest)], check=True)
        self._assert_preserved(dest / "Roamux.app")

    def test_dmg_preserves_symlinks_and_exec_bits(self):
        if not _has("hdiutil"):
            self.skipTest("hdiutil unavailable (non-macOS)")
        out = self.tmp / "Roamux.dmg"
        package_roamux.package_dmg(self.app, out, volname="Roamux")
        mount = subprocess.run(
            ["hdiutil", "attach", str(out), "-nobrowse", "-readonly",
             "-mountrandom", "/tmp"], capture_output=True, text=True,
            check=True)
        mnt = mount.stdout.strip().split("\t")[-1]
        try:
            self._assert_preserved(pathlib.Path(mnt) / "Roamux.app")
        finally:
            subprocess.run(["hdiutil", "detach", mnt], capture_output=True)


def _exe(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("#!/bin/sh\nexit 0\n")
    path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP |
               stat.S_IXOTH)


def _has(tool):
    return subprocess.run(["which", tool], capture_output=True).returncode == 0


def _rmtree(p):
    import shutil
    shutil.rmtree(p, ignore_errors=True)


if __name__ == "__main__":
    unittest.main()

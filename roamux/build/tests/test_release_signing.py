# SPDX-License-Identifier: Apache-2.0
"""Hermetic tests for the roam-33 release signing/packaging logic — no Apple
credentials, no real codesign (that is the release-job E2E). Covers the pure
seams: the signing-mode gate, the inside-out combined sign order, Sparkle
nested-code discovery, and packaging symlink/exec-bit preservation."""

import contextlib
import importlib
import io
import os
import pathlib
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

REL = pathlib.Path(__file__).resolve().parent.parent.parent / "app" / "release"
sys.path.insert(0, str(REL))

import package_roamux  # noqa: E402
import rename_bundle  # noqa: E402
import signing_mode  # noqa: E402
import roamux_signing_config  # noqa: E402
import signing_plan  # noqa: E402
import sign_roamux  # noqa: E402


# ---------------------------------------------------------------------------
# roam-97: checkout-backed signing-package gate. The parts-path + config-seam
# tests exercise Chromium's real `signing.*` package. Tier-1 CI has no checkout,
# so those tests SKIP there. A runner WITH the checkout sets
# REQUIRE_SIGNING_PARTS=1 to turn the skip into a HARD RUN (mirrors roam-132's
# REQUIRE_GRIT). The checkout-free config-shape/derivation tests always run.
# ---------------------------------------------------------------------------
def _resolve_chromium_src():
    src = os.environ.get("ROAMUX_CHROMIUM_SRC") or os.path.expanduser(
        "~/chromium/src")
    return pathlib.Path(src)


CHROMIUM_SRC = _resolve_chromium_src()
_SIGNING_MAC = CHROMIUM_SRC / "chrome" / "installer" / "mac"


def _signing_skip_reason():
    """None when Chromium's signing package imports, else an honest reason."""
    if not (_SIGNING_MAC / "signing" / "parts.py").is_file():
        return ("no Chromium signing package at {} (set ROAMUX_CHROMIUM_SRC)"
                .format(_SIGNING_MAC))
    if str(_SIGNING_MAC) not in sys.path:
        sys.path.insert(0, str(_SIGNING_MAC))
    try:
        for m in ("signing.parts", "signing.config", "signing.model",
                  "signing.config_factory", "signing.driver",
                  "signing.pipeline"):
            importlib.import_module(m)
    except Exception as e:  # noqa: BLE001 — any import failure is an honest skip
        return "signing package not importable from {}: {}".format(
            _SIGNING_MAC, e)
    return None


SIGNING_SKIP = _signing_skip_reason()
REQUIRE_SIGNING_PARTS = os.environ.get("REQUIRE_SIGNING_PARTS") == "1"
_SKIP_PARTS = bool(SIGNING_SKIP) and not REQUIRE_SIGNING_PARTS


def _import_signing():
    """Import and return (parts, config, model) from the real checkout."""
    _signing_skip_reason()  # ensures _SIGNING_MAC is on sys.path
    return (importlib.import_module("signing.parts"),
            importlib.import_module("signing.config"),
            importlib.import_module("signing.model"))


def _make_chromium_base(config_mod):
    """A concrete Chromium-like base over the REAL `signing.config.CodeSignConfig`
    with the unbranded values (product == app_product == "Chromium"). No-arg
    friendly (stub invoker + ad-hoc identity) so get_parts can be driven without
    a build; Model B's RoamuxCodeSignConfig subclasses this."""

    class _ChromiumLikeBase(config_mod.CodeSignConfig):
        def __init__(self, **kwargs):
            kwargs.setdefault("invoker", lambda cfg: None)
            kwargs.setdefault("identity", "-")
            super().__init__(**kwargs)

        @staticmethod
        def is_chrome_branded():
            return False

        @property
        def enable_updater(self):
            return False

        @property
        def app_product(self):
            return "Chromium"

        @property
        def product(self):
            return "Chromium"

        @property
        def version(self):
            return "149.0.7827.201"

        @property
        def base_bundle_id(self):
            return "org.chromium.Chromium"

    return _ChromiumLikeBase


class _StubChromiumBase:
    """Mirrors ONLY the path-deriving computed properties of Chromium's
    signing/config.py CodeSignConfig, so Model-B derivation can be checked
    checkout-free. `product` is the real unbranded value "Chromium"."""

    @property
    def product(self):
        return "Chromium"

    @property
    def app_product(self):
        return "Chromium"

    @property
    def base_bundle_id(self):
        return "org.chromium.Chromium"

    @property
    def app_dir(self):  # config.py: '{.app_product}.app'
        return "{.app_product}.app".format(self)

    @property
    def framework_dir(self):  # config.py formula
        return ("{0.app_dir}/Contents/Frameworks/"
                "{0.product} Framework.framework").format(self)


def _make_renamed_bundle(root):
    """Build an on-disk bundle matching rename_bundle.py's Model-B output: the
    outer app is Roamux.app; the inner framework + helper apps keep their
    Chromium names. Returns the Roamux.app path."""
    app = root / "Roamux.app"
    fw = app / "Contents" / "Frameworks" / "Chromium Framework.framework"
    helpers = fw / "Helpers"
    libs = fw / "Libraries"
    (app / "Contents" / "MacOS").mkdir(parents=True)
    _exe(app / "Contents" / "MacOS" / "Roamux")
    helpers.mkdir(parents=True)
    libs.mkdir(parents=True)
    for h in ("Chromium Helper.app", "Chromium Helper (Renderer).app",
              "Chromium Helper (GPU).app", "Chromium Helper (Alerts).app"):
        (helpers / h / "Contents" / "MacOS").mkdir(parents=True)
        _exe(helpers / h / "Contents" / "MacOS" / h[:-len(".app")])
    for f in ("chrome_crashpad_handler", "app_mode_loader",
              "web_app_shortcut_copier"):
        _exe(helpers / f)
    for lib in ("libEGL.dylib", "libGLESv2.dylib", "libvk_swiftshader.dylib"):
        _exe(libs / lib)
    return app


SECRETS = [
    "ROAMUX_DEVELOPER_ID_CERT_P12",
    "ROAMUX_DEVELOPER_ID_CERT_PASSWORD",
    "ROAMUX_NOTARY_KEY_ID",
    "ROAMUX_NOTARY_ISSUER_ID",
    "ROAMUX_NOTARY_PRIVATE_KEY",
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
        self.assertIn("ROAMUX_NOTARY_KEY_ID", msg)
        self.assertNotIn("ROAMUX_DEVELOPER_ID_CERT_P12", msg)

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
    def test_config_is_model_b_outer_only_rebrand(self):
        # roam-97 Model B: only the OUTER app rebrands. app_product -> "Roamux"
        # and base_bundle_id -> "com.roamux.Roamux", but `product` is INHERITED
        # from the base (unbranded Chromium == "Chromium") and MUST NOT be
        # overridden — else get_parts() would look for nonexistent "Roamux
        # Framework.framework"/"Roamux Helper*.app".
        cls = roamux_signing_config.make_roamux_config_class(_StubChromiumBase)
        cfg = cls()
        self.assertEqual(cfg.app_product, "Roamux")
        self.assertEqual(cfg.base_bundle_id, "com.roamux.Roamux")
        self.assertEqual(cfg.product, "Chromium", "product must be inherited")
        self.assertNotEqual(cfg.product, "Roamux",
                            "regression: product must NOT be overridden (Model B)")

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

    def _make_chromium_app(self, tmp):
        import plistlib
        contents = tmp / "Chromium.app" / "Contents"
        (contents / "MacOS").mkdir(parents=True)
        _exe(contents / "MacOS" / "Chromium")
        with open(contents / "Info.plist", "wb") as f:
            plistlib.dump({"CFBundleExecutable": "Chromium",
                           "CFBundleName": "Chromium",
                           "CFBundleIdentifier": "org.chromium.Chromium",
                           "CFBundleVersion": "7827.201",
                           "CFBundleShortVersionString": "149.0.7827.201"}, f)
        return tmp / "Chromium.app"

    def test_bundle_version_stamped_from_tag(self):
        # roam-120: the appcast advertises the tag version; the installed bundle must
        # carry the SAME scheme in CFBundleVersion, or Sparkle's standard comparator
        # ranks Chromium's 7827.x above every tag version and never offers an update.
        import plistlib
        tmp = pathlib.Path(tempfile.mkdtemp(prefix="roamux-rn-"))
        self.addCleanup(_rmtree, tmp)
        new = rename_bundle.rename_bundle(self._make_chromium_app(tmp),
                                          bundle_version="0.0.1-alpha.2")
        with open(new / "Contents" / "Info.plist", "rb") as f:
            plist = plistlib.load(f)
        self.assertEqual(plist["CFBundleVersion"], "0.0.1-alpha.2",
                         "CFBundleVersion must carry the tag-derived version")
        self.assertEqual(plist["CFBundleShortVersionString"], "149.0.7827.201",
                         "Chromium provenance stays in the short version string")

    def test_without_bundle_version_plist_version_is_untouched(self):
        import plistlib
        tmp = pathlib.Path(tempfile.mkdtemp(prefix="roamux-rn-"))
        self.addCleanup(_rmtree, tmp)
        new = rename_bundle.rename_bundle(self._make_chromium_app(tmp))
        with open(new / "Contents" / "Info.plist", "rb") as f:
            plist = plistlib.load(f)
        self.assertEqual(plist["CFBundleVersion"], "7827.201",
                         "no stamp requested -> no version rewrite")


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


class RoamuxPartsPathTest(unittest.TestCase):
    """roam-97 Done: derive Chromium's part paths for the Model-B Roamux config
    and prove they resolve against the post-rename on-disk bundle — the hermetic
    guard that finding 1 (product/app_product coupling) cannot recur."""

    # ---- checkout-free: the derived path STRINGS (config.py formulas) ----
    def test_derived_paths_are_model_b(self):
        cfg = roamux_signing_config.make_roamux_config_class(
            _StubChromiumBase)()
        self.assertEqual(cfg.app_dir, "Roamux.app")  # app_product -> Roamux
        self.assertEqual(  # product inherited "Chromium"
            cfg.framework_dir,
            "Roamux.app/Contents/Frameworks/Chromium Framework.framework")
        helper = "{0.framework_dir}/Helpers/{0.product} Helper.app".format(cfg)
        self.assertEqual(
            helper,
            "Roamux.app/Contents/Frameworks/Chromium Framework.framework/"
            "Helpers/Chromium Helper.app")

    def test_derived_paths_exist_on_renamed_bundle(self):
        tmp = pathlib.Path(tempfile.mkdtemp(prefix="roamux-parts-"))
        self.addCleanup(_rmtree, tmp)
        _make_renamed_bundle(tmp)
        cfg = roamux_signing_config.make_roamux_config_class(
            _StubChromiumBase)()
        self.assertTrue((tmp / cfg.app_dir).is_dir())
        self.assertTrue((tmp / cfg.framework_dir).is_dir())
        helper = "{0.framework_dir}/Helpers/{0.product} Helper.app".format(cfg)
        self.assertTrue((tmp / helper).is_dir())

    # ---- checkout-backed: Chromium's REAL parts.get_parts() resolves ----
    @unittest.skipIf(_SKIP_PARTS, SIGNING_SKIP or "signing package unavailable")
    def test_real_get_parts_paths_resolve_on_fixture(self):
        parts_mod, config_mod, _model = _import_signing()
        base = _make_chromium_base(config_mod)
        cfg = roamux_signing_config.make_roamux_config_class(base)()
        parts = parts_mod.get_parts(cfg)
        # Model B: outer app rebrands, nested parts keep Chromium names.
        self.assertEqual(parts["app"].path, "Roamux.app")
        self.assertEqual(
            parts["framework"].path,
            "Roamux.app/Contents/Frameworks/Chromium Framework.framework")
        self.assertEqual(
            parts["helper-app"].path,
            "Roamux.app/Contents/Frameworks/Chromium Framework.framework/"
            "Helpers/Chromium Helper.app")
        tmp = pathlib.Path(tempfile.mkdtemp(prefix="roamux-realparts-"))
        self.addCleanup(_rmtree, tmp)
        _make_renamed_bundle(tmp)
        for name, part in parts.items():
            self.assertTrue(
                (tmp / part.path).exists(),
                "nested part {} missing on renamed bundle: {}".format(
                    name, part.path))


class RoamuxSignerSeamTest(unittest.TestCase):
    """roam-97 findings 1/2/3: the config seam actually governs the signer, the
    invocation is well-formed (input DIRECTORY + output, no --entitlements,
    app-signing only), and a usable signed app is promoted."""

    @unittest.skipIf(_SKIP_PARTS, SIGNING_SKIP or "signing package unavailable")
    def test_config_seam_and_output_contract(self):
        parts_mod, config_mod, model_mod = _import_signing()
        import signing.config_factory as cf

        fake_base = _make_chromium_base(config_mod)
        fake_get_class = lambda: fake_base  # noqa: E731
        original_get_class = cf.get_class
        cf.get_class = fake_get_class
        self.addCleanup(setattr, cf, "get_class", original_get_class)

        tmp = pathlib.Path(tempfile.mkdtemp(prefix="roamux-seam-"))
        self.addCleanup(_rmtree, tmp)
        input_dir = tmp / "in"
        input_dir.mkdir()
        app = _make_renamed_bundle(input_dir)               # in/Roamux.app
        output_dir = tmp / "out"
        # Simulate the signer leaving the bare signed app where pipeline.py does.
        _make_renamed_bundle(output_dir / "stable")         # out/stable/Roamux.app
        # A BUILT signing package under the --input dir (build_props_config.py
        # present) so _resolve_signing_pkg_dir picks it — the source tree lacks
        # that generated file (roam-97 Fix 1).
        built_sig = input_dir / "Chromium Packaging" / "signing"
        built_sig.mkdir(parents=True)
        (built_sig / "build_props_config.py").write_text("# generated\n")

        buf = io.StringIO()
        with mock.patch.dict(os.environ,
                             {"ROAMUX_CHROMIUM_OUT": "", "CHROMIUM_OUT": ""}), \
             mock.patch.object(sign_roamux, "sign_sparkle_parts") as sp, \
             mock.patch.object(sign_roamux, "_run") as run_mock, \
             mock.patch("signing.driver._show_tool_versions"), \
             mock.patch("signing.pipeline.sign_all",
                        new_callable=mock.AsyncMock) as sign_all, \
             contextlib.redirect_stdout(buf):
            rc = sign_roamux.main([
                "--mode", "signed", "--identity", "ABCDEF",
                "--app", str(app), "--output", str(output_dir)])

        self.assertEqual(rc, 0)
        self.assertTrue(sign_all.called, "Chromium pipeline was not driven")
        paths_arg = sign_all.call_args.args[0]
        config_arg = sign_all.call_args.args[1]

        # (a) the config reaching the pipeline is the Roamux subclass (Model B).
        self.assertEqual(type(config_arg).__name__, "RoamuxCodeSignConfig")
        self.assertEqual(config_arg.app_product, "Roamux")
        self.assertEqual(config_arg.product, "Chromium")   # inherited
        self.assertEqual(config_arg.base_bundle_id, "com.roamux.Roamux")

        # (b) model.Paths use an --input DIRECTORY + --output; packaging
        #     disabled; notarize none.
        self.assertEqual(pathlib.Path(paths_arg.input).resolve(),
                         input_dir.resolve())
        self.assertEqual(pathlib.Path(paths_arg.output).resolve(),
                         output_dir.resolve())
        self.assertNotEqual(pathlib.Path(paths_arg.input).resolve(),
                            app.resolve(), "--input must be the DIR, not .app")
        self.assertTrue(sign_all.call_args.kwargs["disable_packaging"])
        self.assertEqual(config_arg.notarize,
                         model_mod.NotarizeAndStapleLevel.NONE)

        # (d) the promoted final signed-app path (staple/package consume it)
        #     exists.
        self.assertTrue(app.exists(), "signed app was not promoted")

        # seam restored; Sparkle signed first.
        self.assertIs(cf.get_class, fake_get_class, "get_class not restored")
        self.assertTrue(sp.called, "Sparkle parts not signed first")

        # roam-97 Fix 2: notarization + stapling deferred to #90 — NO stapler
        # runs here, and the deferral is logged.
        run_mock.assert_not_called()  # _run only wraps codesign/stapler
        out_text = buf.getvalue()
        self.assertIn("#90", out_text)
        self.assertIn("DEFERRED", out_text)
        self.assertNotIn("stapler staple", out_text)


class RoamuxDryRunTest(unittest.TestCase):
    """roam-97 finding 3: production --dry-run does NO real signing — no
    driver/pipeline, no codesign, no stapler — and prints the resolved plan."""

    def test_dry_run_performs_no_real_signing(self):
        tmp = pathlib.Path(tempfile.mkdtemp(prefix="roamux-dry-"))
        self.addCleanup(_rmtree, tmp)
        app = tmp / "Roamux.app"
        (app / "Contents" / "MacOS").mkdir(parents=True)
        _exe(app / "Contents" / "MacOS" / "Roamux")
        out = tmp / "out"

        buf = io.StringIO()
        with mock.patch.dict(os.environ,
                             {"ROAMUX_CHROMIUM_OUT": "", "CHROMIUM_OUT": ""}), \
             mock.patch.object(sign_roamux, "_invoke_chromium_signer") as inv, \
             mock.patch.object(sign_roamux, "sign_sparkle_parts") as sp, \
             mock.patch.object(sign_roamux, "promote_signed_app") as promote, \
             mock.patch.object(sign_roamux, "_run") as run_mock, \
             contextlib.redirect_stdout(buf):
            rc = sign_roamux.main([
                "--mode", "signed", "--identity", "ABCDEF",
                "--app", str(app), "--output", str(out), "--dry-run"])

        self.assertEqual(rc, 0)
        inv.assert_not_called()      # no driver.main / pipeline
        sp.assert_not_called()       # no Sparkle codesign
        promote.assert_not_called()  # no filesystem promote
        run_mock.assert_not_called()  # no codesign / stapler shell-out

        text = buf.getvalue()
        self.assertIn("product=Chromium", text)
        self.assertIn("app_product=Roamux", text)
        self.assertIn("base_bundle_id=com.roamux.Roamux", text)
        # the resolved model.Paths (input dir + output) are previewed
        self.assertIn(str(app.parent.resolve()), text)
        self.assertIn(str(out), text)
        # the final promoted signed-app path is previewed
        final = os.path.join(os.path.abspath(str(out)), "stable", "Roamux.app")
        self.assertIn(final, text)
        # Sparkle parts previewed before the outer app in the sign order
        order_line = next(l for l in text.splitlines()
                          if l.startswith("sign order:"))
        self.assertIn("sparkle:", order_line)
        self.assertLess(order_line.index("sparkle:"),
                        order_line.rindex("app"))


class SignRoamuxCLITest(unittest.TestCase):
    """roam-97 finding 3 regression: the malformed CLI is gone."""

    def test_output_present_and_no_entitlements_flag(self):
        parser = sign_roamux._build_parser()
        opts = set()
        for action in parser._actions:
            opts.update(action.option_strings)
        self.assertIn("--output", opts)
        self.assertNotIn(
            "--entitlements", opts,
            "--entitlements is not a Chromium driver flag (roam-97)")

    def test_sign_chrome_shell_out_is_retired(self):
        src = pathlib.Path(sign_roamux.__file__).read_text()
        self.assertNotIn(
            "sign_chrome.py", src,
            "the sign_chrome.py shell-out must be retired (in-process signer)")

    def test_unsigned_mode_returns_zero_without_output(self):
        rc = sign_roamux.main(
            ["--mode", "unsigned", "--app", "/nonexistent/Roamux.app"])
        self.assertEqual(rc, 0)

    def test_signed_mode_requires_output(self):
        rc = sign_roamux.main(
            ["--mode", "signed", "--identity", "X",
             "--app", "/nonexistent/Roamux.app"])
        self.assertEqual(rc, 2)


class SignerPackageResolutionTest(unittest.TestCase):
    """roam-97 Fix 1: the signer must import the BUILT signing package (with the
    GN-generated build_props_config.py under `<...>/Chromium Packaging/signing/`),
    NOT the source tree (which lacks it and fails with ModuleNotFoundError).
    Hermetic — no checkout needed; only the resolution logic is exercised."""

    def _make_built_pkg(self, root):
        sig = root / "Chromium Packaging" / "signing"
        sig.mkdir(parents=True)
        (sig / "build_props_config.py").write_text("# generated\n")
        (sig / "config_factory.py").write_text("# copied\n")
        return str(root / "Chromium Packaging")

    def test_env_out_dir_is_resolved(self):
        tmp = pathlib.Path(tempfile.mkdtemp(prefix="roamux-pkg-"))
        self.addCleanup(_rmtree, tmp)
        out = tmp / "out"
        pkg = self._make_built_pkg(out)
        self.assertEqual(
            sign_roamux._resolve_signing_pkg_dir(
                str(tmp / "input"), env={"ROAMUX_CHROMIUM_OUT": str(out)}),
            pkg)

    def test_input_dir_fallback_is_resolved(self):
        tmp = pathlib.Path(tempfile.mkdtemp(prefix="roamux-pkg-"))
        self.addCleanup(_rmtree, tmp)
        inp = tmp / "input"
        pkg = self._make_built_pkg(inp)
        self.assertEqual(
            sign_roamux._resolve_signing_pkg_dir(str(inp), env={}), pkg)

    def test_env_out_beats_input_dir(self):
        tmp = pathlib.Path(tempfile.mkdtemp(prefix="roamux-pkg-"))
        self.addCleanup(_rmtree, tmp)
        out = tmp / "out"
        env_pkg = self._make_built_pkg(out)
        inp = tmp / "input"
        self._make_built_pkg(inp)
        self.assertEqual(
            sign_roamux._resolve_signing_pkg_dir(
                str(inp), env={"CHROMIUM_OUT": str(out)}),
            env_pkg)

    def test_source_tree_layout_is_not_picked(self):
        # A source-like layout (signing/ WITHOUT the generated build_props_config.py)
        # must NOT resolve — that is exactly the ModuleNotFoundError trap.
        tmp = pathlib.Path(tempfile.mkdtemp(prefix="roamux-pkg-"))
        self.addCleanup(_rmtree, tmp)
        src = tmp / "input" / "Chromium Packaging" / "signing"
        src.mkdir(parents=True)
        (src / "config_factory.py").write_text("# copied, no build_props\n")
        self.assertIsNone(
            sign_roamux._resolve_signing_pkg_dir(str(tmp / "input"), env={}))

    def test_degrades_cleanly_when_absent(self):
        tmp = pathlib.Path(tempfile.mkdtemp(prefix="roamux-pkg-"))
        self.addCleanup(_rmtree, tmp)
        self.assertIsNone(
            sign_roamux._resolve_signing_pkg_dir(str(tmp / "input"), env={}))


class SigningPartsRequirementGateTest(unittest.TestCase):
    """roam-97 finding 2 (CI gate): the checkout-backed parts + seam tests must
    not silently skip everywhere. On a runner WITH the checkout the tier-2 job
    sets REQUIRE_SIGNING_PARTS=1 to make this a HARD RUN (mirrors REQUIRE_GRIT);
    this self-enforcing guard fails loudly if that contract is violated."""

    def test_signing_package_present_when_required(self):
        if REQUIRE_SIGNING_PARTS:
            self.assertIsNone(
                SIGNING_SKIP,
                "REQUIRE_SIGNING_PARTS=1 but the Chromium signing package is "
                "unavailable ({}) — the load-bearing parts-path + config-seam "
                "tests would not execute. The tier-2 CI gate must run with a "
                "synced Chromium checkout (ROAMUX_CHROMIUM_SRC).".format(
                    SIGNING_SKIP))
        else:
            self.skipTest("REQUIRE_SIGNING_PARTS not set — tier-1 skip "
                          "behaviour is unchanged")


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

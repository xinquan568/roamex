# SPDX-License-Identifier: Apache-2.0
"""roam-33 / roam-97 sign driver — resolves the signing mode and, in signed
mode, signs the universal2 Roamux.app inside-out with the Sparkle parts injected
BEFORE the outer app (so the outer seal stays valid), then promotes it.

Model B (roam-97). Chromium's mac signer (chrome/installer/mac/signing) is
consumed as a LIBRARY, IN-PROCESS — not shelled out to. Four things make the
dormant signed path internally consistent:

  1. Config seam. Chromium's `driver.main` resolves its config solely through
     `signing.config_factory.get_class()` (there is NO CLI flag to inject a
     subclass; the only hook is `--development`). So the Roamux config is
     installed by monkeypatching `config_factory.get_class` to return
     `RoamuxCodeSignConfig` for the duration of the call, then RESTORING the
     original in a `finally`. `RoamuxCodeSignConfig` rebrands only the outer app
     (`app_product` -> "Roamux") and inherits `product` == "Chromium", so the
     nested framework/helper part paths resolve to the on-disk Chromium bundles
     `rename_bundle.py` leaves.

  2. BUILT-package import. `config_factory.get_class()` needs the GN-generated
     `signing.build_props_config`, which exists ONLY in the built
     `<root_out_dir>/Chromium Packaging/signing/` — never in
     `CHROMIUM_SRC/chrome/installer/mac` (source). So the signer is imported
     from the BUILT package, resolved from $ROAMUX_CHROMIUM_OUT / $CHROMIUM_OUT
     or the `--input` `Chromium Packaging` dir (see `_resolve_signing_pkg_dir`).
     The plan/preview path imports NO source `signing`, so the built package is
     never shadowed by a cached source copy.

  3. App-signing-only + output contract. Chromium's `pipeline.sign_all` copies
     the app input->work, signs it in the work dir, and its *product* is a
     packaged DMG/PKG in `--output` — the bare signed `.app` is not left usable
     in `--output` by default. Roamux owns packaging (its zip + Sparkle EdDSA),
     so the signer is driven for APP-SIGNING ONLY
     (`--disable-packaging --notarize none`). Under that mode the bare signed
     app lands at `<output>/stable/<app_product>.app` (see
     `signed_app_output_path`); Roamux promotes it onto the release path.

  4. CLI contract. `--input` is the DIRECTORY containing Roamux.app (not the
     `.app` path); `--output` is a separate required dir; there is NO
     `--entitlements` flag (entitlements are config/packaging-derived).

Notarization + stapling (the real signed E2E) are DEFERRED to #90: signed mode
signs the Sparkle parts + outer app and promotes the result, but does NOT run
`xcrun stapler staple` (which requires a completed notarization ticket this
dormant path does not obtain). The --notary-* args are threaded but unused; #90
wires the notarytool submit + staple. `--dry-run` routes through a no-side-effect
planning function and never calls the real Sparkle codesign, Chromium's
driver/pipeline, or the stapler.
"""

import argparse
import os
import pathlib
import shutil
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import signing_mode  # noqa: E402
import signing_plan  # noqa: E402
import roamux_signing_config  # noqa: E402

# The subdirectory Chromium's pipeline places the default (unbranded, no-channel)
# distribution's signed app under, when driven with `--disable-packaging
# --notarize none`. Derived from pipeline._intermediate_work_dir_name(default
# Distribution) -> "stable". See signed_app_output_path().
_SIGNER_DIST_SUBDIR = "stable"

# Model B: config.product is inherited "Chromium", so chrome/installer/mac/
# BUILD.gn copies the BUILT signing package (and GENERATES build_props_config.py)
# under "<root_out_dir>/Chromium Packaging/signing/". Chromium's driver also
# expects `--input` to contain that "Chromium Packaging/" dir. See
# _resolve_signing_pkg_dir().
_PACKAGING_PRODUCT = "Chromium"
_PACKAGING_SUBDIR = _PACKAGING_PRODUCT + " Packaging"


def _run(cmd, dry_run):
    print("+ " + " ".join(str(c) for c in cmd))
    if not dry_run:
        subprocess.run(cmd, check=True)


def _resolve_signing_pkg_dir(input_dir, env=None):
    """Return the directory that must be on sys.path to import the BUILT Chromium
    `signing` package — the one carrying the GN-generated `build_props_config.py`
    that `config_factory.get_class()` requires — or None if it can't be located.

    chrome/installer/mac/BUILD.gn copies the signer to
    `<root_out_dir>/<product> Packaging/signing/` and GENERATES
    `<root_out_dir>/<product> Packaging/signing/build_props_config.py` there — it
    does NOT exist in the source tree (`CHROMIUM_SRC/chrome/installer/mac`), so
    importing the signing package from source fails with ModuleNotFoundError
    before the Roamux seam even runs. Chromium's driver also expects `--input` to
    contain that `<product> Packaging/` dir (`model.Paths.packaging_dir` joins
    input + '<product> Packaging'). Under Model B `config.product` == "Chromium".
    Resolve, in order:
      1. $ROAMUX_CHROMIUM_OUT / $CHROMIUM_OUT  ->  <out>/Chromium Packaging
      2. the --input directory                  ->  <input>/Chromium Packaging
    A candidate qualifies ONLY if it contains `signing/build_props_config.py`
    (the generated file that distinguishes a BUILT package from the source tree).
    """
    env = os.environ if env is None else env
    candidates = []
    out = env.get("ROAMUX_CHROMIUM_OUT") or env.get("CHROMIUM_OUT")
    if out:
        candidates.append(pathlib.Path(out) / _PACKAGING_SUBDIR)
    if input_dir:
        candidates.append(pathlib.Path(input_dir) / _PACKAGING_SUBDIR)
    for c in candidates:
        if (c / "signing" / "build_props_config.py").is_file():
            return str(c)
    return None


def signed_app_output_path(output_dir, app_product="Roamux"):
    """Absolute path at which Chromium's pipeline leaves the bare signed `.app`
    when the signer is driven for APP-SIGNING ONLY (`--disable-packaging
    --notarize none`).

    Confirmed against chrome/installer/mac/signing/pipeline.py:
      * `sign_all()` -> `_sign_and_maybe_notarize_distributions()`: with
        `disable_packaging=True` and `config.notarize == NONE`, for the default
        Distribution `do_packaging` is False and `should_notarize()` is False,
        so `dest_dir = paths.output` joined with
        `_intermediate_work_dir_name(dist)`.
      * For the default Distribution (channel=None, no customization) that
        intermediate directory name is the literal "stable".
      * `_customize_and_sign_chrome()` then MOVES the signed bundle to
        `os.path.join(dest_dir, dist_config.app_dir)`, i.e.
        `<output>/stable/<app_product>.app`.
    `model.Paths` abspath()s output, so this returns the absolute path.
    """
    return os.path.join(
        os.path.abspath(str(output_dir)), _SIGNER_DIST_SUBDIR,
        "{}.app".format(app_product))


def _built_part_keys(input_dir, env=None):
    """Ordered Chromium part keys from the BUILT signing package (the same one
    driver.main will use), outer app ('app') last — or None if the built package
    can't be resolved/imported. Informational only: feeds the plan preview's
    sign-order line. NEVER imports the source `signing` tree (which lacks
    build_props_config.py and would shadow the built package once cached)."""
    pkg_dir = _resolve_signing_pkg_dir(input_dir, env)
    if pkg_dir is None:
        return None
    if pkg_dir not in sys.path:
        sys.path.insert(0, pkg_dir)
    try:
        import importlib
        cf = importlib.import_module("signing.config_factory")
        parts_mod = importlib.import_module("signing.parts")
        roamux_cls = roamux_signing_config.make_roamux_config_class(
            cf.get_class())
        cfg = roamux_cls(invoker=lambda c: None, identity="-")
        keys = list(parts_mod.get_parts(cfg).keys())
        if "app" in keys:  # Chromium keys the outer app 'app'; ensure it is last.
            keys = [k for k in keys if k != "app"] + ["app"]
        return keys
    except Exception:  # noqa: BLE001 — preview only; degrade to static keys
        return None


def _config_identity():
    """The Model-B config identity for the plan preview — deterministic constants.

    Chromium's config couples `app_product` (outer app) and `product` (nested
    parts); Model B rebrands only the outer app, so `product` is inherited
    "Chromium" while `app_product`/`base_bundle_id` are Roamux. Reading these
    back off a live config would require importing the BUILT signing package
    (build_props_config.py) — that import is deferred to `_invoke_chromium_signer`
    at signing time — so the preview uses the known constants and imports
    nothing (no source `signing` gets cached)."""
    return {"product": "Chromium", "app_product": "Roamux",
            "base_bundle_id": "com.roamux.Roamux"}


def build_signing_plan(args, identity):
    """Resolve the full signing plan with NO side effects beyond an optional
    read-only import of the BUILT signing package for the sign-order preview
    (used by --dry-run and as the input to the real signed run)."""
    input_dir = str(pathlib.Path(args.app).resolve().parent)
    release_app = str(pathlib.Path(args.app).resolve())
    output_dir = args.output
    signer_app = signed_app_output_path(output_dir)
    config = _config_identity()
    chromium_keys = _built_part_keys(input_dir) or [
        "chromium_framework", "chromium_helpers", "app"]
    ordered = roamux_signing_config.roamux_get_parts({}, chromium_keys)
    outer = chromium_keys[-1]
    # Invariants: outer app sealed last; Sparkle parts before it.
    assert ordered[-1] == outer, "outer app must be signed last"
    for k in roamux_signing_config.sparkle_part_keys():
        assert ordered.index(k) < ordered.index(outer), \
            "Sparkle parts must precede the outer app"
    return {
        "identity": identity,
        "input_dir": input_dir,
        "output_dir": output_dir,
        "release_app": release_app,
        "signer_app": signer_app,
        "config": config,
        "ordered": ordered,
        "outer_key": outer,
    }


def print_plan(plan):
    c = plan["config"]
    print("=== roam-97 signed-release plan (Model B) ===")
    print("config: RoamuxCodeSignConfig")
    print("  product={} app_product={} base_bundle_id={}".format(
        c["product"], c["app_product"], c["base_bundle_id"]))
    print("paths (model.Paths): input={} output={}".format(
        plan["input_dir"], plan["output_dir"]))
    print("  --input is the DIRECTORY containing {}.app; app-signing only "
          "(--disable-packaging --notarize none); NO --entitlements".format(
              c["app_product"]))
    print("sign order: " + " -> ".join(plan["ordered"]))
    print("final signed app (from signer) -> {}".format(plan["signer_app"]))
    print("promoted to release path -> {}".format(plan["release_app"]))
    print("notarization + stapling: DEFERRED to #90 (real signed E2E)")


def sign_sparkle_parts(app_path, identity, dry_run):
    """Sign every nested Sparkle bundle deepest-first, same identity, hardened
    runtime. Returns the ordered parts actually planned (for verification)."""
    framework = (pathlib.Path(app_path) / "Contents" / "Frameworks" /
                 "Sparkle.framework")
    parts = signing_plan.discover_sparkle_parts(framework)
    signing_plan.assert_sparkle_fully_planned(framework, parts)
    for part in parts:
        _run(["codesign", "--force", "--sign", identity,
              "--options", "runtime", "--timestamp", str(part)], dry_run)
    return parts


def _invoke_chromium_signer(identity, input_dir, output_dir):
    """Drive Chromium's signer IN-PROCESS for APP-SIGNING ONLY, with the Roamux
    config installed via the config_factory seam (restored in a finally).

    Imports the BUILT signing package (resolved from $ROAMUX_CHROMIUM_OUT /
    $CHROMIUM_OUT or the --input `Chromium Packaging` dir) — NOT the source tree —
    because `config_factory.get_class()` needs the GN-generated
    build_props_config.py, which lives only in the built package. Nothing in the
    plan path imports source `signing`, so the built package is never shadowed by
    a cached source copy.

    `--disable-packaging --notarize none`: Roamux owns packaging; notarization +
    stapling are DEFERRED to #90. Returns the installed Roamux config class."""
    pkg_dir = _resolve_signing_pkg_dir(input_dir)
    if pkg_dir is None:
        raise RuntimeError(
            "cannot locate the BUILT Chromium signing package (with the "
            "GN-generated build_props_config.py). Set ROAMUX_CHROMIUM_OUT to the "
            "build output dir, or ensure <input>/{} exists.".format(
                _PACKAGING_SUBDIR))
    if pkg_dir not in sys.path:
        sys.path.insert(0, pkg_dir)
    import signing.config_factory as cf
    import signing.driver as driver

    roamux_cls = roamux_signing_config.make_roamux_config_class(cf.get_class())
    original_get_class = cf.get_class
    cf.get_class = lambda: roamux_cls
    try:
        driver.main([
            "--identity", identity,
            "--input", str(input_dir),
            "--output", str(output_dir),
            "--disable-packaging",
            "--notarize", "none",
        ])
    finally:
        cf.get_class = original_get_class
    return roamux_cls


def promote_signed_app(output_dir, release_app):
    """Copy/promote the bare signed app from the signer's output location
    (`<output>/stable/Roamux.app`) onto the release path staple/package
    consume, preserving symlinks."""
    src = signed_app_output_path(output_dir)
    release_app = str(release_app)
    print("+ promote signed app {} -> {}".format(src, release_app))
    if not os.path.exists(src):
        raise FileNotFoundError(
            "signer did not leave a signed app at {} (expected "
            "<output>/stable/<app_product>.app per pipeline.py)".format(src))
    if os.path.lexists(release_app):
        if os.path.islink(release_app) or os.path.isfile(release_app):
            os.remove(release_app)
        else:
            shutil.rmtree(release_app)
    shutil.copytree(src, release_app, symlinks=True)
    return release_app


def run_signed(args, plan):
    """Execute the real signed pipeline (never called under --dry-run)."""
    # 1) Sparkle nested code first (deepest-first) so the outer app seals it.
    sign_sparkle_parts(args.app, plan["identity"], dry_run=False)
    # 2) Chromium's signer (config seam, app-signing only) writes the signed app
    #    to <output>/stable/Roamux.app.
    _invoke_chromium_signer(plan["identity"], plan["input_dir"],
                            plan["output_dir"])
    # 3) Promote the signed app onto the release path.
    promote_signed_app(plan["output_dir"], plan["release_app"])
    # 4) roam-97: the app is signed + promoted. Notarization and stapling — the
    #    real signed E2E — are DEFERRED to #90. We intentionally do NOT run
    #    `xcrun stapler staple` here: stapling REQUIRES a completed notarization
    #    ticket, and no notarytool submission happens in this dormant path. The
    #    --notary-* args are threaded but unused; #90 wires the notarytool submit
    #    + staple. Returning success means "app signed + promoted".
    print("signing-mode=signed — Sparkle parts + outer app signed and promoted "
          "to {}. Notarization + stapling DEFERRED to #90 (real signed E2E); "
          "the --notary-* args are unused here.".format(plan["release_app"]))
    return 0


def _build_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True,
                        help="the universal2 Roamux.app to sign")
    parser.add_argument("--output", default="",
                        help="output DIRECTORY for the signer (required in "
                             "signed mode); the bare signed app is retrieved "
                             "from <output>/stable/Roamux.app")
    parser.add_argument("--identity", default="",
                        help="Developer ID identity (signed mode)")
    # Notary credentials: threaded but UNUSED in roam-97 — notarization +
    # stapling (the real signed E2E) are deferred to #90, which wires the
    # `xcrun notarytool submit` + `xcrun stapler staple` steps. Kept so the
    # release caller's interface is stable across the #90 cutover.
    parser.add_argument("--notary-key", default="")
    parser.add_argument("--notary-key-id", default="")
    parser.add_argument("--notary-issuer", default="")
    parser.add_argument("--mode", choices=("signed", "unsigned"), default="",
                        help="explicit mode (else derived from the env gate)")
    # NOTE (roam-97 finding 3): NO --entitlements. Chromium's driver has no such
    # flag; entitlements are config/packaging-derived.
    parser.add_argument("--dry-run", action="store_true")
    return parser


def main(argv=None):
    parser = _build_parser()
    args = parser.parse_args(argv)

    if args.mode:
        mode = args.mode
    else:
        try:
            mode = signing_mode.resolve_signing_mode(os.environ)
        except signing_mode.PartialSigningSecretsError as e:
            print("::error::{}".format(e), file=sys.stderr)
            return 2

    if mode == "unsigned":
        print("signing-mode=unsigned — deliberate personal-alpha; "
              "skipping Apple codesign/notarize/staple. The Sparkle EdDSA "
              "signature (roam-32) still applies.")
        return 0

    identity = args.identity or os.environ.get("ROAMUX_SIGN_IDENTITY", "")
    if not identity:
        print("::error::signed mode but no signing identity resolved",
              file=sys.stderr)
        return 2

    if not args.output:
        print("::error::signed mode requires --output (the signer output "
              "directory; the signed app is retrieved from "
              "<output>/stable/Roamux.app)", file=sys.stderr)
        return 2

    plan = build_signing_plan(args, identity)
    print_plan(plan)

    if args.dry_run:
        print("dry-run: NO signing performed (Sparkle codesign, Chromium "
              "driver/pipeline, and stapler all skipped).")
        return 0

    return run_signed(args, plan)


if __name__ == "__main__":
    sys.exit(main())

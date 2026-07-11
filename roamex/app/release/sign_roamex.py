# SPDX-License-Identifier: Apache-2.0
"""roam-33 sign driver — resolves the signing mode and, in signed mode, signs
the universal2 Roamux.app inside-out with the Sparkle parts injected BEFORE the
outer app (so the outer seal stays valid), then staples.

Signed mode reuses Chromium's signing package (chrome/installer/mac) for the
base app's nested-helper order and drives codesign for the Sparkle parts with
the SAME identity. Unsigned mode logs the state and skips all Apple steps
(the Sparkle EdDSA signature from roam-32 is orthogonal and always present).

The real codesign/notarize/staple only runs in the release job with the
protected credentials; this module is unit-covered for its decision + plan
construction and dry-runnable elsewhere.
"""

import argparse
import os
import pathlib
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import signing_mode  # noqa: E402
import signing_plan  # noqa: E402
import roamex_signing_config  # noqa: F401,E402


def _run(cmd, dry_run):
    print("+ " + " ".join(str(c) for c in cmd))
    if not dry_run:
        subprocess.run(cmd, check=True)


def _chromium_part_keys(base_cls, chromium_src):
    """Ordered part keys from Chromium's own get_parts, outer app last."""
    import importlib
    import sys
    mac = str(pathlib.Path(chromium_src) / "chrome" / "installer" / "mac")
    if mac not in sys.path:
        sys.path.insert(0, mac)
    parts_mod = importlib.import_module("signing.parts")
    cfg = base_cls()
    keys = list(parts_mod.get_parts(cfg).keys())
    # Chromium keys the outer app as 'app'; ensure it is last.
    if "app" in keys:
        keys = [k for k in keys if k != "app"] + ["app"]
    return keys


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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True, help="the universal2 .app")
    parser.add_argument("--identity", default="",
                        help="Developer ID identity (signed mode)")
    parser.add_argument("--notary-key", default="")
    parser.add_argument("--notary-key-id", default="")
    parser.add_argument("--notary-issuer", default="")
    parser.add_argument("--mode", choices=("signed", "unsigned"), default="",
                        help="explicit mode (else derived from the env gate)")
    parser.add_argument("--entitlements", default=str(
        pathlib.Path(__file__).resolve().parent / "entitlements" /
        "roamex.entitlements"))
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    if args.mode:
        mode = args.mode
    else:
        try:
            mode = signing_mode.resolve_signing_mode(os.environ)
        except signing_mode.PartialSigningSecretsError as e:
            print(f"::error::{e}", file=sys.stderr)
            return 2

    if mode == "unsigned":
        print("signing-mode=unsigned — deliberate personal-alpha; "
              "skipping Apple codesign/notarize/staple. The Sparkle EdDSA "
              "signature (roam-32) still applies.")
        return 0

    identity = args.identity or os.environ.get("ROAMEX_SIGN_IDENTITY", "")
    if not identity:
        print("::error::signed mode but no signing identity resolved",
              file=sys.stderr)
        return 2

    # Build the Roamux-extended config over Chromium's real config class (the
    # config seam) — sets Roamux.app product/bundle-id so sign_chrome operates
    # on the renamed bundle — and derive the ordered part keys with Sparkle
    # injected before the outer app.
    chromium_src = os.environ.get("CHROMIUM_SRC", ".")
    base = roamex_signing_config.load_chromium_config_base(chromium_src)
    if base is not None:
        roamex_cfg_cls = roamex_signing_config.make_roamex_config_class(base)
        print(f"using {roamex_cfg_cls.__name__} "
              f"(product={roamex_cfg_cls().app_product})")
        chromium_keys = _chromium_part_keys(base, chromium_src)
    else:
        print("Chromium signing package not importable here — plan preview "
              "only (real signing runs on the release builder).")
        chromium_keys = ["chromium_framework", "chromium_helpers", "app"]
    ordered = roamex_signing_config.roamex_get_parts({}, chromium_keys)
    assert ordered[-1] == chromium_keys[-1]  # outer app sealed last
    for k in roamex_signing_config.sparkle_part_keys():
        assert ordered.index(k) < ordered.index(chromium_keys[-1])
    print("sign order: " + " -> ".join(ordered))

    # Sparkle nested code first (deepest-first), then Chromium's pipeline signs
    # the base app + nested helpers + the OUTER APP LAST, sealing over Sparkle.
    sign_sparkle_parts(args.app, identity, args.dry_run)
    # Drive Chromium's signing package for the base app + its nested helpers +
    # the outer app (last), with the Roamux outer entitlements + hardened
    # runtime; sign_chrome derives the ordered parts from RoamexCodeSignConfig.
    _run([sys.executable,
          str(pathlib.Path(os.environ.get("CHROMIUM_SRC", "."))
              / "chrome" / "installer" / "mac" / "sign_chrome.py"),
          "--identity", identity, "--input", args.app, "--notarize",
          "--entitlements", args.entitlements,
          "--notary-arg", f"--key={args.notary_key}",
          "--notary-arg", f"--key-id={args.notary_key_id}",
          "--notary-arg", f"--issuer={args.notary_issuer}"], args.dry_run)
    _run(["xcrun", "stapler", "staple", args.app], args.dry_run)
    print("signing-mode=signed — Sparkle parts + outer app signed, stapled.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

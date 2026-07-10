# SPDX-License-Identifier: Apache-2.0
"""roam-33 sign driver — resolves the signing mode and, in signed mode, signs
the universal2 Roamex.app inside-out with the Sparkle parts injected BEFORE the
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

    # Sparkle parts first (nested, deepest-first), then Chromium's pipeline
    # signs the base app's nested helpers and the OUTER APP LAST — sealing over
    # the already-signed Sparkle (signing_plan.combined_sign_plan encodes the
    # invariant asserted by the tests).
    sparkle = sign_sparkle_parts(args.app, identity, args.dry_run)
    plan = signing_plan.combined_sign_plan(
        chromium_parts=[], sparkle_parts=[str(p) for p in sparkle],
        outer_app=args.app)
    assert plan[-1] == args.app  # outer app is sealed last
    # Drive Chromium's signing package for the base app + its nested helpers +
    # the outer app (last), with the Roamex outer entitlements + hardened
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

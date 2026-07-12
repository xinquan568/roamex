#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Bundle icon gate (roam-103): assert a built bundle carries the Roamux bold-X icon payloads.

Upstream ships the mac app icon through two channels — Resources/app.icns (CFBundleIconFile) and
Resources/Assets.car (CFBundleIconName=AppIcon; the one modern macOS displays). Patch 0029 rewires
both bundle_data sources to roamux/app/resources/icons/mac/; this checker proves the swap actually
landed. Invariant, applied to the outer bundle AND the Alerts helper when present (the helper
consumes the same bundle_data targets and plist keys):

  Contents/Resources/app.icns   == <repo>/app/resources/icons/mac/app.icns   (bytes)
  Contents/Resources/Assets.car == <repo>/app/resources/icons/mac/Assets.car (bytes)
  Info.plist CFBundleIconFile == "app.icns", CFBundleIconName == "AppIcon"

Name-agnostic (works on Chromium.app pre-rename and Roamux.app post-rename); a missing helper is
noted, not fatal. Run after `autoninja chrome` (dev) or against ${ROAMUX_APP} (release).

Usage: check_app_icon.py --app <bundle.app> --repo <overlay root> [--json]
"""

import argparse
import pathlib
import plistlib
import sys

EXPECTED_ICON_FILE = "app.icns"
EXPECTED_ICON_NAME = "AppIcon"


def _read_plist(path):
    try:
        with open(path, "rb") as f:
            return plistlib.load(f)
    except (FileNotFoundError, plistlib.InvalidFileException, ValueError):
        return None


def check_bundle(app, expected_icns, expected_car):
    """Apply the full icon invariant to one bundle; return a list of failure strings."""
    app = pathlib.Path(app)
    failures = []
    res = app / "Contents" / "Resources"
    for leaf, expected in (("app.icns", expected_icns), ("Assets.car", expected_car)):
        payload = res / leaf
        if not payload.is_file():
            failures.append(f"{app.name}: missing Contents/Resources/{leaf}")
        elif payload.read_bytes() != expected:
            failures.append(f"{app.name}: Contents/Resources/{leaf} does not match the vendored payload")
    plist = _read_plist(app / "Contents" / "Info.plist")
    if plist is None:
        failures.append(f"{app.name}: missing or unreadable Contents/Info.plist")
        return failures
    if plist.get("CFBundleIconFile") != EXPECTED_ICON_FILE:
        failures.append(f"{app.name}: CFBundleIconFile != {EXPECTED_ICON_FILE!r} "
                        f"(got {plist.get('CFBundleIconFile')!r})")
    if plist.get("CFBundleIconName") != EXPECTED_ICON_NAME:
        failures.append(f"{app.name}: CFBundleIconName != {EXPECTED_ICON_NAME!r} "
                        f"(got {plist.get('CFBundleIconName')!r})")
    return failures


def find_alerts_helpers(app):
    """The Alerts helper rides the framework's versioned Helpers dir; glob, never hardcode."""
    return sorted(pathlib.Path(app).glob(
        "Contents/Frameworks/*.framework/Versions/*/Helpers/* (Alerts).app"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True, help="path to the built .app bundle")
    parser.add_argument("--repo", required=True,
                        help="the roamux/ overlay root holding app/resources/icons/mac/")
    parser.add_argument("--json", action="store_true",
                        help="emit a one-line JSON summary on stdout")
    args = parser.parse_args()

    icons = pathlib.Path(args.repo) / "app" / "resources" / "icons" / "mac"
    try:
        expected_icns = (icons / "app.icns").read_bytes()
        expected_car = (icons / "Assets.car").read_bytes()
    except FileNotFoundError as e:
        print(f"[fail] vendored payload missing: {e.filename}", file=sys.stderr)
        return 2

    # --json contract: stdout carries ONLY the JSON summary; human lines go to stderr.
    human_out = sys.stderr if args.json else sys.stdout

    bundles = [pathlib.Path(args.app)]
    helpers = find_alerts_helpers(args.app)
    if helpers:
        bundles.extend(helpers)
    else:
        print("[note] no Alerts helper found under Contents/Frameworks — checking the outer "
              "bundle only", file=human_out)

    failures = []
    for bundle in bundles:
        failures.extend(check_bundle(bundle, expected_icns, expected_car))

    for failure in failures:
        print(f"[fail] {failure}", file=sys.stderr)
    if not failures:
        print(f"[ok] {len(bundles)} bundle(s) carry the Roamux icon payloads + plist keys",
              file=human_out)
    if args.json:
        import json
        print(json.dumps({"ok": not failures, "bundles_checked": len(bundles),
                          "failures": failures}))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

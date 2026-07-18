#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Dark-logo gate (roam-169): the dark-mode logo payload is the real Roamux glyph, in sync with
its patch.

Dark mode bypasses chrome://theme/current-channel-logo (roam-158): the shared cr-toolbar/cr-drawer
<picture> selects its dark <source> -> //resources/images/chrome_logo_dark.svg, and the
chrome://version body logo rides chrome://theme/IDR_PRODUCT_LOGO{,_WHITE}. Patch
0038-dark-logo-roamux.patch closes both: it replaces upstream chrome_logo_dark.svg's content with
the committed payload (roamux/app/resources/theme/chrome_logo_dark.svg — the roam-157 glyph at
intrinsic size 24) and rewrites about_version.html's desktop <picture> to a single
current-channel-logo <img>.

This checker proves, tier-1 (pure Python, no Chromium build):
  1. the committed payload is structurally the Roamux glyph — four palette ray <line>s, the
     luminance-mask knockout, no tile <rect>, no drop-shadow <filter>, root width/height 24 —
     so a stub/placeholder can never silently ship (the roam-140/roam-157 lesson);
  2. patch 0038 stays in sync — its chrome_logo_dark.svg added lines equal the payload exactly,
     and its about_version.html hunk removes every chrome://theme/IDR_PRODUCT_LOGO reference
     while adding the current-channel-logo srcset — so the patch cannot drift from the asset.

Usage: check_dark_logo.py [--payload <svg>] [--patch <patch>] [--json]
"""

import argparse
import json
import pathlib
import sys
import xml.etree.ElementTree as ET

_SVG = "{http://www.w3.org/2000/svg}"
PALETTE = {"#4285F4", "#EA4335", "#FBBC05", "#34A853"}
INTRINSIC_SIZE = "24"

PAYLOAD_REL = pathlib.Path("app") / "resources" / "theme" / "chrome_logo_dark.svg"
PATCH_REL = pathlib.Path("patches") / "0038-dark-logo-roamux.patch"

UPSTREAM_SVG_PATH = "ui/webui/resources/images/chrome_logo_dark.svg"
UPSTREAM_VERSION_PATH = "components/webui/version/resources/about_version.html"


def _tag(el):
    return el.tag.replace(_SVG, "")


def _masked_ids(root):
    inside = set()
    for el in root.iter():
        if _tag(el) == "mask":
            for sub in el.iter():
                inside.add(id(sub))
    return inside


def check_payload(payload_path):
    """Failure strings for the committed glyph payload (empty list = pass)."""
    payload_path = pathlib.Path(payload_path)
    if not payload_path.is_file():
        return [f"{payload_path}: missing"]
    try:
        root = ET.fromstring(payload_path.read_bytes())
    except ET.ParseError as err:
        return [f"{payload_path}: not parseable XML ({err})"]
    failures = []
    if root.get("width") != INTRINSIC_SIZE or root.get("height") != INTRINSIC_SIZE:
        failures.append(
            f"{payload_path}: root width/height must be {INTRINSIC_SIZE}"
            f" (got {root.get('width')}x{root.get('height')}) — the upstream"
            " asset's intrinsic size")
    strokes = {el.get("stroke") for el in root.iter()
               if _tag(el) == "line" and el.get("stroke") in PALETTE}
    if len(strokes) != len(PALETTE):
        failures.append(
            f"{payload_path}: expected the 4 palette ray strokes, found"
            f" {sorted(strokes)} (flat/placeholder stub?)")
    if not any(_tag(el) == "mask" for el in root.iter()):
        failures.append(
            f"{payload_path}: no luminance mask — the transparent-centre"
            " knockout is missing")
    masked = _masked_ids(root)
    for el in root.iter():
        if id(el) in masked:
            continue
        if _tag(el) == "rect":
            failures.append(
                f"{payload_path}: visible <rect> outside the mask — the glyph"
                " must not ship the tile background")
        if _tag(el) in ("filter", "feDropShadow"):
            failures.append(
                f"{payload_path}: drop-shadow/filter present — the glyph is"
                " flat art")
    return failures


def _patch_file_sections(patch_text):
    """{upstream-path: (added, removed, context)} parsed per diff file section.

    Context lines matter: a hunk can carry an unchanged line (leading space)
    that survives into the patched file — the sync check must see those too,
    or an IDR reference retained as context would slip past tier-1.
    """
    sections = {}
    current = None
    in_hunk = False
    for line in patch_text.splitlines():
        if line.startswith("diff --git "):
            # diff --git a/<path> b/<path>
            current = line.split(" b/", 1)[-1].strip()
            sections[current] = ([], [], [])
            in_hunk = False
        elif current and line.startswith("@@"):
            in_hunk = True
        elif current and line.startswith("+") and not line.startswith("+++"):
            sections[current][0].append(line[1:])
        elif current and line.startswith("-") and not line.startswith("---"):
            sections[current][1].append(line[1:])
        elif current and in_hunk and line.startswith(" "):
            sections[current][2].append(line[1:])
    return sections


def check_patch_sync(payload_path, patch_path):
    """Failure strings for patch 0038's content vs the payload (empty = pass)."""
    payload_path = pathlib.Path(payload_path)
    patch_path = pathlib.Path(patch_path)
    if not patch_path.is_file():
        return [f"{patch_path}: missing — the dark-logo fix has not shipped"]
    if not payload_path.is_file():
        return [f"{payload_path}: missing — nothing to sync the patch against"]
    sections = _patch_file_sections(patch_path.read_text())
    failures = []

    svg = sections.get(UPSTREAM_SVG_PATH)
    if svg is None:
        failures.append(f"{patch_path}: no diff section for {UPSTREAM_SVG_PATH}")
    else:
        added = svg[0]
        payload_lines = payload_path.read_text().splitlines()
        if added != payload_lines:
            failures.append(
                f"{patch_path}: {UPSTREAM_SVG_PATH} added lines differ from the"
                f" committed payload ({len(added)} vs {len(payload_lines)}"
                " lines) — regenerate the patch from the payload")

    version = sections.get(UPSTREAM_VERSION_PATH)
    if version is None:
        failures.append(
            f"{patch_path}: no diff section for {UPSTREAM_VERSION_PATH}")
    else:
        added, removed, context = version
        # Added AND context lines both survive into the patched file.
        if any("IDR_PRODUCT_LOGO" in line for line in added + context):
            failures.append(
                f"{patch_path}: {UPSTREAM_VERSION_PATH} leaves an"
                " IDR_PRODUCT_LOGO reference in the patched file"
                " (added or retained as hunk context)")
        if not any("IDR_PRODUCT_LOGO" in line for line in removed):
            failures.append(
                f"{patch_path}: {UPSTREAM_VERSION_PATH} hunk does not remove"
                " the IDR_PRODUCT_LOGO references")
        for marker in ("current-channel-logo@1x", "current-channel-logo@2x"):
            if not any(marker in line for line in added):
                failures.append(
                    f"{patch_path}: {UPSTREAM_VERSION_PATH} hunk does not add"
                    f" the {marker} srcset")
    return failures


def check_all(payload_path, patch_path):
    return check_payload(payload_path) + check_patch_sync(payload_path, patch_path)


def main(argv=None):
    root = pathlib.Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--payload", type=pathlib.Path, default=root / PAYLOAD_REL)
    parser.add_argument("--patch", type=pathlib.Path, default=root / PATCH_REL)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)

    failures = check_all(args.payload, args.patch)
    if args.json:
        print(json.dumps({"ok": not failures, "failures": failures}, indent=2))
    else:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        if not failures:
            print("dark-logo check: OK")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

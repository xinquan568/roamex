#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Omnibox-chip icon gate (roam-159): the Roamux colour vector icon is real and patch 0039 stays
surgical.

The chrome:// omnibox chip renders roamux::kRoamuxProductIcon
(roamux/browser/ui/icons/vector_icons/roamux_product.icon), returned from the kChromeUIScheme arm
of ChromeLocationBarModelDelegate::GetVectorIconOverride() by patch
0039-omnibox-chip-roamux-icon.patch. This checker proves, tier-1 (pure Python):

  1. the .icon is the real fixed-colour Roamux mark: exactly the four palette colours as
     PATH_COLOR_ARGB rows, at least four stroked ray paths, round caps (no CAP_SQUARE opt-out),
     CANVAS_DIMENSIONS 16 — a tinted/stub icon cannot silently ship;
  2. patch 0039 is surgical: it touches ONLY the delegate .cc and the toolbar BUILD.gn, swaps
     ONLY the kChromeUIScheme return (kProductChromeRefreshIcon out, the roamux icon in), and
     never touches the kGoogleColorIcon / kExtensionChromeRefreshIcon arms — the issue's
     no-regression criterion for non-chrome:// states rides this proof (the security/error paths
     are not in the patch at all). Added AND context lines are inspected (the roam-169 lesson).

Usage: check_omnibox_icon.py [--icon <path>] [--patch <path>] [--json]
"""

import argparse
import json
import pathlib
import re
import sys

PALETTE = ("0xEA, 0x43, 0x35", "0xFB, 0xBC, 0x05",
           "0x34, 0xA8, 0x53", "0x42, 0x85, 0xF4")
CANVAS = "CANVAS_DIMENSIONS, 16"

ICON_REL = pathlib.Path("browser") / "ui" / "icons" / "vector_icons" / "roamux_product.icon"
PATCH_REL = pathlib.Path("patches") / "0039-omnibox-chip-roamux-icon.patch"

DELEGATE_PATH = "chrome/browser/ui/toolbar/chrome_location_bar_model_delegate.cc"
TOOLBAR_GN_PATH = "chrome/browser/ui/toolbar/BUILD.gn"
ALLOWED_PATCH_FILES = {DELEGATE_PATH, TOOLBAR_GN_PATH}
OTHER_ARMS = ("kGoogleColorIcon", "kExtensionChromeRefreshIcon")


def check_icon(icon_path):
    """Failure strings for the .icon file (empty list = pass)."""
    icon_path = pathlib.Path(icon_path)
    if not icon_path.is_file():
        return [f"{icon_path}: missing"]
    text = icon_path.read_text()
    failures = []
    if CANVAS not in text:
        failures.append(f"{icon_path}: expected '{CANVAS}'")
    # EXACTLY the four palette rows, in ray order — an extra or off-palette
    # fixed colour is as wrong as a missing one.
    rows = re.findall(r"^PATH_COLOR_ARGB, 0xFF, (.+?),\s*$", text,
                      flags=re.MULTILINE)
    if rows != list(PALETTE):
        failures.append(
            f"{icon_path}: PATH_COLOR_ARGB rows {rows} != the expected"
            f" ordered palette {list(PALETTE)} — the chip must carry exactly"
            " the four ray colours")
    strokes = len(re.findall(r"^STROKE,", text, flags=re.MULTILINE))
    if strokes < 4:
        failures.append(
            f"{icon_path}: {strokes} stroked paths, expected >= 4 rays")
    if "CAP_SQUARE" in text:
        failures.append(
            f"{icon_path}: CAP_SQUARE present — the rays use the default round"
            " caps")
    return failures


def _patch_file_sections(patch_text):
    """{path: (added, removed, context, hunk_headers)} — context and the
    hunk-header anchors matter: they pin WHERE a hunk lands (roam-169 +
    this issue's step-8 hardening)."""
    sections = {}
    current = None
    in_hunk = False
    for line in patch_text.splitlines():
        if line.startswith("diff --git "):
            current = line.split(" b/", 1)[-1].strip()
            sections[current] = ([], [], [], [])
            in_hunk = False
        elif current and line.startswith("@@"):
            in_hunk = True
            sections[current][3].append(line.split("@@")[-1].strip())
        elif current and line.startswith("+") and not line.startswith("+++"):
            sections[current][0].append(line[1:])
        elif current and line.startswith("-") and not line.startswith("---"):
            sections[current][1].append(line[1:])
        elif current and in_hunk and line.startswith(" "):
            sections[current][2].append(line[1:])
    return sections


def check_patch(patch_path):
    """Failure strings for patch 0039's surgicality (empty list = pass)."""
    patch_path = pathlib.Path(patch_path)
    if not patch_path.is_file():
        return [f"{patch_path}: missing — the omnibox-chip fix has not shipped"]
    sections = _patch_file_sections(patch_path.read_text())
    failures = []
    extra = set(sections) - ALLOWED_PATCH_FILES
    if extra:
        failures.append(
            f"{patch_path}: touches files beyond the declared surface:"
            f" {sorted(extra)}")
    delegate = sections.get(DELEGATE_PATH)
    if delegate is None:
        failures.append(f"{patch_path}: no diff section for {DELEGATE_PATH}")
    else:
        added, removed, context, _headers = delegate
        if not any("kRoamuxProductIcon" in line for line in added):
            failures.append(
                f"{patch_path}: delegate hunk does not return the Roamux icon")
        if not any("kProductChromeRefreshIcon" in line for line in removed):
            failures.append(
                f"{patch_path}: delegate hunk does not remove the Chromium"
                " product return")
        # The swap must land INSIDE the kChromeUIScheme arm: the unchanged
        # scheme test must frame the hunk as context (a wrong-arm swap would
        # carry a different scheme line, or none).
        if not any("url.SchemeIs(content::kChromeUIScheme)" in line
                   for line in context):
            failures.append(
                f"{patch_path}: delegate return-swap hunk is not anchored in"
                " the kChromeUIScheme arm (scheme test absent from context)")
        for arm in OTHER_ARMS:
            if any(arm in line for line in added + removed):
                failures.append(
                    f"{patch_path}: delegate hunk touches the {arm} arm — the"
                    " patch must swap only the kChromeUIScheme return")
    gn = sections.get(TOOLBAR_GN_PATH)
    if gn is None:
        failures.append(f"{patch_path}: no diff section for {TOOLBAR_GN_PATH}")
    else:
        gn_added, _gn_removed, gn_context, gn_headers = gn
        if not any("//roamux/browser/ui/icons" in line for line in gn_added):
            failures.append(
                f"{patch_path}: toolbar BUILD.gn hunk lacks the"
                " //roamux/browser/ui/icons dep edge")
        # The dep must land in source_set("toolbar")'s non-Android deps block:
        # the git hunk header names the enclosing target, and the block's
        # sibling deps frame the insertion as context.
        if not any('source_set("toolbar")' in header for header in gn_headers):
            failures.append(
                f"{patch_path}: toolbar BUILD.gn hunk is not anchored in"
                ' source_set("toolbar") (hunk header)')
        non_android_siblings = ("//components/user_education/common",
                                "//chrome/browser/ui/tabs:tab_strip")
        if not any(any(sib in line for sib in non_android_siblings)
                   for line in gn_context):
            failures.append(
                f"{patch_path}: toolbar BUILD.gn dep edge is not framed by the"
                " non-Android deps block's siblings — wrong scope?")
    return failures


def check_all(icon_path, patch_path):
    return check_icon(icon_path) + check_patch(patch_path)


def main(argv=None):
    root = pathlib.Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--icon", type=pathlib.Path, default=root / ICON_REL)
    parser.add_argument("--patch", type=pathlib.Path, default=root / PATCH_REL)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    failures = check_all(args.icon, args.patch)
    if args.json:
        print(json.dumps({"ok": not failures, "failures": failures}, indent=2))
    else:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        if not failures:
            print("omnibox-icon check: OK")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

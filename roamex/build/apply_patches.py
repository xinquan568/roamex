#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Roamex patch runhook (execution plan §12.2 mechanism 3, §12.5).

Applies every ``roamex/patches/*.patch`` to a Chromium checkout, in name order.
Idempotent: an already-applied patch is skipped (detected via ``git apply --reverse --check``).
Fails loudly, naming the patch, when one neither is applied nor applies cleanly.

Usage:
  apply_patches.py --chromium-src ~/chromium/src [--patches DIR] [--check]

``--check`` verifies (applied or cleanly appliable) without mutating the tree.
"""

import argparse
import pathlib
import subprocess
import sys


def _git_apply(chromium_src, patch, *flags):
    return subprocess.run(
        ["git", "-C", str(chromium_src), "apply", *flags, str(patch)],
        capture_output=True, text=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--chromium-src", required=True, type=pathlib.Path,
                        help="path to the Chromium src/ checkout")
    parser.add_argument("--patches", type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parent.parent / "patches",
                        help="directory holding *.patch files (default: roamex/patches)")
    parser.add_argument("--check", action="store_true",
                        help="verify only; do not mutate the tree")
    args = parser.parse_args()

    patches = sorted(args.patches.glob("*.patch"))
    if not patches:
        print(f"no patches found under {args.patches}", file=sys.stderr)
        return 0

    for patch in patches:
        if _git_apply(args.chromium_src, patch, "--reverse", "--check").returncode == 0:
            print(f"[applied]   {patch.name}")
            continue
        if _git_apply(args.chromium_src, patch, "--check").returncode == 0:
            if args.check:
                print(f"[appliable] {patch.name}")
            else:
                result = _git_apply(args.chromium_src, patch)
                if result.returncode != 0:
                    _fail(patch, result)
                    return 1
                print(f"[apply]     {patch.name}")
            continue
        _fail(patch, _git_apply(args.chromium_src, patch, "--check"))
        return 1
    return 0


def _fail(patch, result):
    print(f"FAIL: patch neither applied nor cleanly appliable: {patch.name}", file=sys.stderr)
    if result.stderr:
        print(result.stderr, file=sys.stderr)
    print("Rebase or fix the patch (§12.5: patches fail loudly on rebase).", file=sys.stderr)


if __name__ == "__main__":
    sys.exit(main())

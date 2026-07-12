#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Override-staleness gate (execution plan §12.5).

Every file under ``roamux/chromium_src/`` shadows an upstream Chromium file. This tool records a
sha256 of the *pristine* upstream content — read with ``git show <pin>:<path>`` at the revision named
in ``roamux/build/CHROMIUM_PIN``, never from the (patched) working tree — into
``override_signatures.json``. On a milestone uprev, any override whose upstream counterpart changed
fails loudly so the copy is consciously re-reviewed.

Pin resolution is strict: a missing/UNPINNED/unfetched pin is a hard failure (no HEAD fallback).

Usage:
  check_override_staleness.py --chromium-src ~/chromium/src            # gate (default mode)
  check_override_staleness.py --chromium-src ~/chromium/src --update   # re-record after a reviewed uprev
"""

import argparse
import hashlib
import json
import pathlib
import subprocess
import sys

_SKIP_NAMES = {"README.md", ".gitkeep", ".DS_Store"}


def _read_pin(pin_file):
    if not pin_file.exists():
        return ""
    for line in pin_file.read_text().splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            return line
    return ""


def _pristine_sha256(chromium_src, ref, rel_path):
    result = subprocess.run(["git", "-C", str(chromium_src), "show", f"{ref}:{rel_path}"],
                            capture_output=True)
    if result.returncode != 0:
        return None
    return hashlib.sha256(result.stdout).hexdigest()


def _override_paths(overlay):
    root = overlay / "chromium_src"
    if not root.is_dir():
        return []
    return sorted(str(f.relative_to(root)) for f in root.rglob("*")
                  if f.is_file() and f.name not in _SKIP_NAMES)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--chromium-src", required=True, type=pathlib.Path)
    parser.add_argument("--overlay", type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parent.parent,
                        help="overlay root containing chromium_src/ (default: roamux/)")
    parser.add_argument("--pin-file", type=pathlib.Path, default=None,
                        help="default: <overlay>/build/CHROMIUM_PIN")
    parser.add_argument("--manifest", type=pathlib.Path, default=None,
                        help="default: <overlay>/build/override_signatures.json")
    parser.add_argument("--update", action="store_true",
                        help="re-record signatures at the current pin (after a reviewed uprev)")
    args = parser.parse_args()

    pin_file = args.pin_file or args.overlay / "build" / "CHROMIUM_PIN"
    manifest_path = args.manifest or args.overlay / "build" / "override_signatures.json"

    pin = _read_pin(pin_file)
    if not pin or pin == "UNPINNED":
        print(f"FAIL: {pin_file} is missing or UNPINNED — pin Chromium first (BOOTSTRAP.md step 2).",
              file=sys.stderr)
        return 1
    # Tag-only resolution: a bare commit-ish (HEAD, a branch, a SHA) must NOT satisfy the pin
    # gate — only the milestone tag named in CHROMIUM_PIN counts (Step-5/Step-8 finding).
    pin_ref = f"refs/tags/{pin}"
    resolves = subprocess.run(
        ["git", "-C", str(args.chromium_src), "rev-parse", "--verify", f"{pin_ref}^{{commit}}"],
        capture_output=True)
    if resolves.returncode != 0:
        print(f"FAIL: tag '{pin}' does not resolve in {args.chromium_src}.\n"
              f"hint: git -C {args.chromium_src} fetch --depth=1 origin "
              f"+refs/tags/{pin}:refs/tags/{pin}", file=sys.stderr)
        return 1

    current = {}
    problems = []
    for rel_path in _override_paths(args.overlay):
        digest = _pristine_sha256(args.chromium_src, pin_ref, rel_path)
        if digest is None:
            problems.append(f"{rel_path}: no upstream counterpart at {pin} "
                            "(override of a nonexistent file?)")
        else:
            current[rel_path] = digest

    if args.update:
        if problems:
            for problem in problems:
                print(f"FAIL: {problem}", file=sys.stderr)
            return 1
        manifest_path.write_text(json.dumps({"pin": pin, "signatures": current}, indent=2,
                                            sort_keys=True) + "\n")
        print(f"recorded {len(current)} override signature(s) at {pin} -> {manifest_path}")
        return 0

    if not manifest_path.exists():
        print(f"FAIL: manifest {manifest_path} missing — run with --update to record signatures.",
              file=sys.stderr)
        return 1
    manifest = json.loads(manifest_path.read_text())
    recorded = manifest.get("signatures", {})

    for rel_path, digest in current.items():
        if rel_path not in recorded:
            problems.append(f"UNREGISTERED override: {rel_path} — review it, then run --update")
        elif recorded[rel_path] != digest:
            problems.append(f"STALE override: {rel_path} — upstream changed "
                            f"(recorded at {manifest.get('pin')}, checking {pin}); "
                            "re-review the copy, then run --update")
    for rel_path in recorded:
        if rel_path not in current:
            print(f"warn: manifest entry with no override file (removed?): {rel_path}")

    if problems:
        for problem in problems:
            print(f"FAIL: {problem}", file=sys.stderr)
        return 1
    print(f"OK: {len(current)} override(s) fresh at {pin}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

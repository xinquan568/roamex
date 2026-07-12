#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Fetch the pinned Sparkle.framework release (roam-32, plan §13.6/K4, R16).

Downloads the pinned Sparkle release archive, verifies its SHA-256 BEFORE
extraction (supply-chain integrity — the pin is the trust anchor), and
extracts it into roamux/third_party/sparkle/ (gitignored except README.md).
Idempotent: an existing Sparkle.framework at the destination is a no-op
unless --force. Builds with roamux_enable_sparkle=true fail loudly when the
framework is absent and name this script.

Usage:
  roamux/build/fetch_sparkle.py [--dest DIR] [--force]
  # test/offline overrides:
  roamux/build/fetch_sparkle.py --archive /path/to/archive.tar.xz \
      [--expected-sha256 HEX]
"""

import argparse
import hashlib
import pathlib
import shutil
import subprocess
import sys
import tempfile
import urllib.request

SPARKLE_VERSION = "2.9.4"
SPARKLE_URL = ("https://github.com/sparkle-project/Sparkle/releases/download/"
               f"{SPARKLE_VERSION}/Sparkle-{SPARKLE_VERSION}.tar.xz")
SPARKLE_SHA256 = (
    "ce89daf967db1e1893ed3ebd67575ed82d3902563e3191ca92aaec9164fbdef9")


def _sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--dest", type=pathlib.Path,
        default=pathlib.Path(__file__).resolve().parent.parent /
        "third_party" / "sparkle")
    parser.add_argument("--force", action="store_true",
                        help="re-fetch even if the framework exists")
    parser.add_argument("--archive", type=pathlib.Path,
                        help="use a local archive instead of downloading")
    parser.add_argument("--expected-sha256",
                        help="override the pinned hash (tests only)")
    args = parser.parse_args()

    framework = args.dest / "Sparkle.framework"
    if framework.is_dir() and not args.force:
        print(f"[ok] Sparkle.framework already present at {args.dest} "
              f"(--force to re-fetch)")
        return 0

    expected = args.expected_sha256 or SPARKLE_SHA256
    with tempfile.TemporaryDirectory(prefix="roamux-sparkle-") as tmp:
        if args.archive:
            archive = args.archive
        else:
            archive = pathlib.Path(tmp) / "sparkle.tar.xz"
            print(f"[fetch] {SPARKLE_URL}")
            urllib.request.urlretrieve(SPARKLE_URL, archive)

        actual = _sha256(archive)
        if actual != expected:
            print(f"FAIL: SHA-256 mismatch for {archive}\n"
                  f"  expected {expected}\n  actual   {actual}\n"
                  "Refusing to extract (R16: the pin is the trust anchor).",
                  file=sys.stderr)
            return 1

        extract_dir = pathlib.Path(tmp) / "extract"
        extract_dir.mkdir()
        # tar preserves the framework's symlink layout; Sparkle deltas and
        # codesign break if flattened.
        subprocess.run(["tar", "-xJf", str(archive), "-C", str(extract_dir)],
                       check=True)
        source = extract_dir / "Sparkle.framework"
        if not source.is_dir():
            print("FAIL: archive did not contain Sparkle.framework",
                  file=sys.stderr)
            return 1

        if framework.is_dir():
            shutil.rmtree(framework)
        args.dest.mkdir(parents=True, exist_ok=True)
        shutil.move(str(source), str(framework))
        for extra in ("LICENSE", "CHANGELOG"):
            src = extract_dir / extra
            if src.is_file():
                shutil.copy2(src, args.dest / extra)
        # I-6.3 will need the release tooling (sign_update etc.).
        bin_dir = extract_dir / "bin"
        if bin_dir.is_dir():
            dest_bin = args.dest / "bin"
            if dest_bin.is_dir():
                shutil.rmtree(dest_bin)
            shutil.move(str(bin_dir), str(dest_bin))

    print(f"[ok] Sparkle {SPARKLE_VERSION} vendored at {args.dest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

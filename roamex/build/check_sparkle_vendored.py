#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build-time gate (roam-32): fail loudly, naming the fetch step, when a
roamex_enable_sparkle=true build runs without the vendored framework."""

import pathlib
import sys


def main():
    sparkle_dir, stamp = sys.argv[1], sys.argv[2]
    binary = pathlib.Path(sparkle_dir) / "Sparkle.framework" / "Versions" / "B" / "Sparkle"
    if not binary.is_file():
        print("FAIL: roamex_enable_sparkle=true but Sparkle.framework is not "
              f"vendored at {sparkle_dir}.\n"
              "Run: python3 roamex/build/fetch_sparkle.py "
              "(see roamex/third_party/sparkle/README.md)", file=sys.stderr)
        return 1
    pathlib.Path(stamp).write_text("ok\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""SPDX Apache-2.0 header check (roam-38, §7.9). Single source of truth: the pre-commit hook and the
CI governance job both invoke this over their file list.

Required on the Roamux-authored code/build/CI/script set below. Exempt: roamux/chromium_src/** (upstream
BSD copies), .md docs (prose is not license-headered), and no-comment data/legal files.
"""

import pathlib
import sys

REQUIRED_SUFFIXES = {".cc", ".h", ".mm", ".ts", ".py", ".sh", ".gn", ".gni", ".yml", ".yaml"}
MARKER = "SPDX-License-Identifier: Apache-2.0"


def is_exempt(path):
    p = path.replace("\\", "/")
    if "roamux/chromium_src/" in p or p.startswith("roamux/chromium_src/"):
        return True
    return pathlib.PurePath(p).suffix not in REQUIRED_SUFFIXES


def main(argv):
    violations = []
    for path in argv:
        if is_exempt(path):
            continue
        try:
            head = "\n".join(pathlib.Path(path).read_text(errors="replace").splitlines()[:3])
        except (FileNotFoundError, IsADirectoryError):
            continue
        if MARKER not in head:
            violations.append(path)
    for v in violations:
        print(f"SPDX: missing '// {MARKER}' in first 3 lines: {v}", file=sys.stderr)
    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

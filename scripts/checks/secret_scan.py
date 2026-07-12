#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Secret scan (roam-38, §7.9). High-signal patterns only, with an allowlist for this repo's known-safe
strings (the google_keys template placeholders, obvious example/fixture tokens). Hook + CI both call it.
"""

import pathlib
import re
import sys

PATTERNS = [
    ("private-key-block", re.compile(r"-----BEGIN [A-Z ]*PRIVATE KEY-----")),
    ("aws-access-key", re.compile(r"\bAKIA[0-9A-Z]{16}\b")),
    ("slack-token", re.compile(r"\bxox[baprs]-[0-9A-Za-z-]{10,}")),
    ("generic-secret", re.compile(
        r"""(?i)\b(secret|token|password|api[_-]?key|client[_-]?secret)\b\s*[:=]\s*["'][^"'\s]{16,}["']""")),
]
# Substrings that make a match a known-safe placeholder/example rather than a real secret.
ALLOWLIST = ("EXAMPLE", "<your", "your-", "placeholder", "REPLACE", "dummy", "fake", "xxxx")
# An explicit, review-visible per-line opt-out — HONORED ONLY in approved suppression paths (test
# fixtures + *.template), so a normal source/config change cannot silence detection and slip a real
# credential past CI. Same idea as detect-secrets' pragma / bandit's nosec, but path-scoped.
ALLOW_MARKER = "roamux:allow-secret"
SUPPRESSIBLE_DIRS = ("roamux/build/tests/", "/tests/")  # a real tests directory
SUPPRESSIBLE_SUFFIXES = (".template",)


def _suppressible(path):
    p = path.replace("\\", "/")
    name = p.rsplit("/", 1)[-1]
    # Only genuine test files (basename test_*.py / *_test.*), files in a tests/ dir, or *.template
    # may carry the marker — NOT an arbitrary source path that merely contains "test_" somewhere.
    return (any(d in p for d in SUPPRESSIBLE_DIRS)
            or name.startswith("test_")
            or "_test." in name
            or any(p.endswith(suf) for suf in SUPPRESSIBLE_SUFFIXES))


def main(argv):
    violations = []
    for path in argv:
        try:
            text = pathlib.Path(path).read_text(errors="replace")
        except (FileNotFoundError, IsADirectoryError):
            continue
        allow_here = _suppressible(path)
        for lineno, line in enumerate(text.splitlines(), 1):
            if allow_here and ALLOW_MARKER in line:
                continue
            for name, pat in PATTERNS:
                if pat.search(line) and not any(a.lower() in line.lower() for a in ALLOWLIST):
                    violations.append(f"{path}:{lineno}: possible {name}")
    for v in violations:
        print(f"secret-scan: {v}", file=sys.stderr)
    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

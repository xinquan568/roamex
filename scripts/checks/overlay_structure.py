#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Overlay-structure check (roam-38, §7.9 / §12.2) — a STRUCTURAL APPROXIMATION only. A single commit
cannot know the whole §12.2 hook inventory, so this enforces cheap shape rules; the roam-2 staleness
gate and the reviewer are the deeper enforcement. Hook + CI both call it.

Rules:
  (1) files under roamux/patches/ match NNNN-slug.patch (or are README.md);
  (2) a Roamux-authored file (carrying our SPDX) must live under a declared Roamux area
      (roamux/, scripts/, .github/, docs/, or top-level) — NOT at an upstream-Chromium mirror path
      (chrome/, content/, base/, ui/, components/, ...) outside roamux/chromium_src/. That mirror-path
      shape is how an undeclared in-tree upstream edit would masquerade as ours; such a file belongs in
      roamux/chromium_src/ (an override) instead.
"""

import pathlib
import re
import sys

PATCH_RE = re.compile(r"^\d{4}-[a-z0-9][a-z0-9-]*\.patch$")
SPDX = "SPDX-License-Identifier: Apache-2.0"
# Top-level dirs that only exist in an upstream Chromium checkout, never in this overlay repo.
# Top-level dirs that only exist in an upstream Chromium checkout, never in this overlay repo.
UPSTREAM_ROOT_NAMES = {"chrome", "content", "base", "ui", "components", "third_party",
                       "services", "net", "cc", "gpu", "media", "v8", "sandbox", "google_apis"}


def _carries_our_spdx(path):
    try:
        head = "\n".join(pathlib.Path(path).read_text(errors="replace").splitlines()[:3])
    except (FileNotFoundError, IsADirectoryError):
        return False
    return SPDX in head


def main(argv):
    violations = []
    for path in argv:
        p = path.replace("\\", "/")
        # (1) patch-name shape
        if "/roamux/patches/" in p or p.startswith("roamux/patches/"):
            name = pathlib.PurePath(p).name
            if not (name == "README.md" or name.startswith(".") or PATCH_RE.match(name)):
                violations.append(f"{path}: patch name must match NNNN-slug.patch")
        # (2) upstream-path masquerade: a Roamux-authored (our-SPDX) file at an upstream mirror path
        # that is NOT under roamux/chromium_src/ is an undeclared in-tree upstream edit.
        first = pathlib.PurePath(p).parts[0] if pathlib.PurePath(p).parts else ""
        if (first in UPSTREAM_ROOT_NAMES and "roamux/chromium_src/" not in p
                and _carries_our_spdx(path)):
            violations.append(
                f"{path}: Roamux-authored file at an upstream mirror path — put overrides under "
                f"roamux/chromium_src/ or additive code under roamux/ (§12.2)")
    for v in violations:
        print(f"overlay-structure: {v}", file=sys.stderr)
    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

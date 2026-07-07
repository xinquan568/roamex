#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""pre-push test gate (roam-38, §7.9 / P6). Honest degradation: always runs the hermetic suite; when a
Chromium checkout is configured (ROAMEX_CHROMIUM_SRC with an out/Default) it also builds+runs the touched
Roamex gtest target. Never a silent pass; never a full-tree build.
"""

import os
import pathlib
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
GTEST_TARGET = "roamex_unittests"


def _clean_git_env():
    # git exports GIT_DIR/GIT_INDEX_FILE/... into hooks; strip them so the fixture tests' throwaway
    # `git -C <tmp>` repos are not hijacked into the real repo.
    return {k: v for k, v in os.environ.items()
            if k not in ("GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_PREFIX",
                         "GIT_COMMON_DIR", "GIT_OBJECT_DIRECTORY")}


def gtest_decision(environ):
    """Return ('run', src) when a usable checkout is configured, else ('skip', reason). Pure — testable."""
    src = environ.get("ROAMEX_CHROMIUM_SRC", "")
    if not src:
        return ("skip", "no Chromium checkout configured (ROAMEX_CHROMIUM_SRC unset)")
    if not (pathlib.Path(src) / "out" / "Default").is_dir():
        return ("skip", f"ROAMEX_CHROMIUM_SRC={src} has no out/Default build dir")
    return ("run", src)


def main():
    print("pre-push: running the hermetic suite (checkout-free)...")
    rc = subprocess.run([sys.executable, "-m", "unittest", "discover", "-s", "roamex/build/tests"],
                        cwd=REPO, env=_clean_git_env()).returncode
    if rc != 0:
        print("pre-push: hermetic suite FAILED — push blocked.", file=sys.stderr)
        return rc

    action, detail = gtest_decision(os.environ)
    if action == "skip":
        print(f"pre-push: SKIP gtest gate — {detail}; the touched-target build+test runs in CI.")
        return 0

    src = detail
    print(f"pre-push: Chromium checkout at {src} — building + running {GTEST_TARGET} (incremental)...")
    build = subprocess.run(["autoninja", "-C", "out/Default", GTEST_TARGET], cwd=src)
    if build.returncode != 0:
        print("pre-push: autoninja FAILED — push blocked.", file=sys.stderr)
        return build.returncode
    test = subprocess.run([str(pathlib.Path(src) / "out" / "Default" / GTEST_TARGET)], cwd=src)
    if test.returncode != 0:
        print(f"pre-push: {GTEST_TARGET} FAILED — push blocked.", file=sys.stderr)
        return test.returncode
    print(f"pre-push: {GTEST_TARGET} green.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

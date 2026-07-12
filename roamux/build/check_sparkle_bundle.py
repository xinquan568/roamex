#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Flag-on bundle smoke gate (roam-32, plan §13.6/K4): assert a
roamux_enable_sparkle=true Chromium.app actually bundles Sparkle correctly and
loads it. Run after `autoninja chrome` on a flag-on build.

Usage: check_sparkle_bundle.py --app out/Default/Chromium.app
"""

import argparse
import os
import plistlib
import subprocess
import sys


def fail(msg):
    print(f"FAIL: {msg}", file=sys.stderr)
    return 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True)
    args = parser.parse_args()
    app = args.app

    fw = os.path.join(app, "Contents", "Frameworks", "Sparkle.framework")
    if not os.path.isdir(fw):
        return fail(f"Sparkle.framework missing under {app}/Contents/Frameworks")
    # Framework symlink layout must survive (Sparkle deltas + codesign break
    # if flattened).
    for link in ("Sparkle", "Headers", "Resources"):
        p = os.path.join(fw, link)
        if not os.path.islink(p):
            return fail(f"Sparkle.framework/{link} is not a symlink "
                        "(archive flattened the framework)")

    info = os.path.join(app, "Contents", "Info.plist")
    with open(info, "rb") as f:
        plist = plistlib.load(f)
    for key in ("SUFeedURL", "SUPublicEDKey", "SUScheduledCheckInterval"):
        if key not in plist:
            return fail(f"Info.plist missing merged Sparkle key {key}")
    if not plist["SUFeedURL"].endswith("/releases/latest/download/appcast.xml"):
        return fail(f"SUFeedURL is not the production /latest/ feed: "
                    f"{plist['SUFeedURL']}")

    # The linking image resolves Sparkle via @rpath.
    exe = os.path.join(app, "Contents", "MacOS",
                       plist["CFBundleExecutable"])
    otool = subprocess.run(["otool", "-L", exe], capture_output=True,
                           text=True)
    if "@rpath/Sparkle.framework" not in otool.stdout:
        return fail("main executable does not load @rpath/Sparkle.framework")

    # Loadability: launch --version with dyld tracing; Sparkle must load.
    env = dict(os.environ, DYLD_PRINT_LIBRARIES="1")
    launch = subprocess.run([exe, "--version"], capture_output=True,
                            text=True, env=env, timeout=60)
    if "Sparkle.framework/Versions" not in launch.stderr:
        return fail("Sparkle did not load at launch (dyld trace)")

    print("[ok] Sparkle bundle smoke gate passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

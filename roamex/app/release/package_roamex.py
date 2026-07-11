# SPDX-License-Identifier: Apache-2.0
"""roam-33 packaging — .zip and .dmg that PRESERVE framework symlinks + exec
bits (Sparkle deltas and codesign break on a flattened framework). `ditto`
preserves symlinks, xattrs, and mode bits; `hdiutil` images a staged dir."""

import argparse
import pathlib
import shutil
import subprocess
import sys
import tempfile


def package_zip(app_path, out_path):
    app_path = pathlib.Path(app_path)
    out_path = pathlib.Path(out_path)
    # ditto -c -k --sequesterRsrc --keepParent: a symlink/xattr/mode-preserving
    # zip that keeps the .app as the archive's top-level entry.
    subprocess.run(
        ["ditto", "-c", "-k", "--sequesterRsrc", "--keepParent",
         str(app_path), str(out_path)],
        check=True)
    return out_path


def package_dmg(app_path, out_path, volname="Roamux"):
    app_path = pathlib.Path(app_path)
    out_path = pathlib.Path(out_path)
    with tempfile.TemporaryDirectory(prefix="roamex-dmg-") as stage:
        # ditto into the staging dir preserves the bundle exactly.
        subprocess.run(["ditto", str(app_path),
                        str(pathlib.Path(stage) / app_path.name)], check=True)
        if out_path.exists():
            out_path.unlink()
        # hdiutil can transiently fail under concurrent /dev/disk pressure;
        # a bounded retry keeps the real release job robust.
        last = None
        for attempt in range(4):
            r = subprocess.run(
                ["hdiutil", "create", "-volname", volname, "-srcfolder",
                 stage, "-ov", "-format", "UDZO", str(out_path)],
                capture_output=True, text=True)
            if r.returncode == 0:
                break
            last = r.stderr
        else:
            raise RuntimeError(f"hdiutil create failed after retries: {last}")
    return out_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True)
    parser.add_argument("--zip")
    parser.add_argument("--dmg")
    parser.add_argument("--volname", default="Roamux")
    args = parser.parse_args()
    if args.zip:
        package_zip(args.app, args.zip)
        print(f"[ok] zip: {args.zip}")
    if args.dmg:
        package_dmg(args.app, args.dmg, args.volname)
        print(f"[ok] dmg: {args.dmg}")
    if not args.zip and not args.dmg:
        print("nothing to do (pass --zip and/or --dmg)", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())

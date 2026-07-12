# SPDX-License-Identifier: Apache-2.0
"""roam-33: rename an unbranded Chromium.app to Roamux.app for release.

The reference build is unbranded (is_chrome_branded=false) so the product is
Chromium.app; rather than pull Google branding surgery, the release pipeline
renames the built bundle: the .app dir, the main executable, and the
Info.plist CFBundleName/CFBundleExecutable/CFBundleIdentifier. Helper apps
under Contents/Frameworks keep their Chromium names (sign_chrome re-signs
them as-is; roam-93 confirmed the code path). Signing happens AFTER this
rename so the seal covers the final names.
"""

import argparse
import pathlib
import plistlib
import shutil
import sys

OLD = "Chromium"
NEW = "Roamux"
NEW_BUNDLE_ID = "com.roamux.Roamux"


def _rewrite_plist(info_plist):
    with open(info_plist, "rb") as f:
        plist = plistlib.load(f)
    if plist.get("CFBundleExecutable") == OLD:
        plist["CFBundleExecutable"] = NEW
    if plist.get("CFBundleName") == OLD:
        plist["CFBundleName"] = NEW
    bid = plist.get("CFBundleIdentifier", "")
    if bid.startswith("org.chromium.Chromium"):
        plist["CFBundleIdentifier"] = bid.replace("org.chromium.Chromium",
                                                   NEW_BUNDLE_ID)
    with open(info_plist, "wb") as f:
        plistlib.dump(plist, f)


def rename_bundle(app_path):
    """Rename Chromium.app -> Roamux.app in place; returns the new path."""
    app_path = pathlib.Path(app_path)
    contents = app_path / "Contents"
    macos = contents / "MacOS"
    old_exe = macos / OLD
    if old_exe.exists():
        old_exe.rename(macos / NEW)
    info = contents / "Info.plist"
    if info.exists():
        _rewrite_plist(info)
    new_app = app_path.with_name(app_path.name.replace(OLD, NEW))
    if new_app != app_path:
        if new_app.exists():
            shutil.rmtree(new_app)
        app_path.rename(new_app)
    return new_app


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True)
    args = parser.parse_args()
    new = rename_bundle(args.app)
    print(f"[ok] renamed -> {new}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

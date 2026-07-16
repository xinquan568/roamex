# SPDX-License-Identifier: Apache-2.0
"""roam-33 / roam-97: rename an unbranded Chromium.app to Roamux.app for release.

The reference build is unbranded (is_chrome_branded=false) so the product is
Chromium.app; rather than pull Google branding surgery, the release pipeline
renames the built bundle: the .app dir, the main executable, and the
Info.plist CFBundleName/CFBundleExecutable/CFBundleIdentifier. Helper apps and
the inner framework under Contents/Frameworks keep their Chromium names.

This is exactly the roam-97 Model-B decision, and it is intentional (not an
oversight): `RoamuxCodeSignConfig` rebrands only the OUTER app
(`app_product` -> "Roamux") and inherits `product` == "Chromium", so Chromium's
`signing/config.py` derives the nested framework/helper part paths as
"Chromium Framework.framework"/"Chromium Helper*.app" — matching precisely what
this script leaves on disk (roam-93 confirmed the re-sign code path). Keeping
the nested Chromium names avoids the Model-A surgery of renaming the inner
framework/helpers and rewiring every @rpath/@executable_path load command.

The output layout — a directory whose only bundle is `Roamux.app` — is what
`sign_roamux.py` passes to Chromium's signer as `--input` (the DIRECTORY
containing the app). Signing happens AFTER this rename so the seal covers the
final names.
"""

import argparse
import pathlib
import plistlib
import shutil
import sys

OLD = "Chromium"
NEW = "Roamux"
NEW_BUNDLE_ID = "com.roamux.Roamux"


def _rewrite_plist(info_plist, bundle_version=None):
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
    if bundle_version:
        # roam-120: Sparkle compares the appcast's sparkle:version against the host's
        # CFBundleVersion. Chromium's BUILD.PATCH (e.g. 7827.201) outranks every tag
        # version, so the bundle must carry the SAME tag-derived scheme the appcast
        # advertises. CFBundleShortVersionString keeps the Chromium provenance.
        plist["CFBundleVersion"] = bundle_version
    with open(info_plist, "wb") as f:
        plistlib.dump(plist, f)


def rename_bundle(app_path, bundle_version=None):
    """Rename Chromium.app -> Roamux.app in place; returns the new path."""
    app_path = pathlib.Path(app_path)
    contents = app_path / "Contents"
    macos = contents / "MacOS"
    old_exe = macos / OLD
    if old_exe.exists():
        old_exe.rename(macos / NEW)
    info = contents / "Info.plist"
    if info.exists():
        _rewrite_plist(info, bundle_version)
    new_app = app_path.with_name(app_path.name.replace(OLD, NEW))
    if new_app != app_path:
        if new_app.exists():
            shutil.rmtree(new_app)
        app_path.rename(new_app)
    return new_app


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True)
    parser.add_argument("--bundle-version",
                        help="tag-derived version stamped into CFBundleVersion so the "
                             "installed bundle and the appcast share one Sparkle-"
                             "comparable scheme (roam-120)")
    args = parser.parse_args()
    new = rename_bundle(args.app, args.bundle_version)
    print(f"[ok] renamed -> {new}"
          + (f" (CFBundleVersion={args.bundle_version})" if args.bundle_version else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())

# SPDX-License-Identifier: Apache-2.0
"""roam-33: the Roamex extension of Chromium's mac signing config (F5).

`RoamexCodeSignConfig` overrides the product/bundle-id names so
`chrome/installer/mac/sign_chrome.py` operates on the renamed Roamex.app, and
`roamex_get_parts()` injects the Sparkle framework + its nested code into
Chromium's parts dict BEFORE the outer app (which Chromium's pipeline signs
last, keeping the outer seal valid). Kept import-light so it is unit-testable
without a Chromium checkout on sys.path: the CodeSignConfig base is imported
lazily inside the factory.
"""

import pathlib

# The nested Sparkle parts, deepest-first, as (key, relative-bundle-path,
# entitlements-basename-or-None) — entitlements only for the outer app; the
# Sparkle helpers inherit hardened runtime via --options runtime.
SPARKLE_PART_PATHS = (
    "Contents/Frameworks/Sparkle.framework/Versions/B/XPCServices/Downloader.xpc",
    "Contents/Frameworks/Sparkle.framework/Versions/B/XPCServices/Installer.xpc",
    "Contents/Frameworks/Sparkle.framework/Versions/B/Updater.app",
    "Contents/Frameworks/Sparkle.framework/Versions/B/Autoupdate",
    "Contents/Frameworks/Sparkle.framework",
)

ENTITLEMENTS_DIR = pathlib.Path(__file__).resolve().parent / "entitlements"


def make_roamex_config_class(base_cls):
    """Given Chromium's CodeSignConfig, return a Roamex subclass. Split out so
    it can be unit-tested with a stub base (no Chromium checkout required)."""

    class RoamexCodeSignConfig(base_cls):
        @property
        def product(self):
            return "Roamex"

        @property
        def app_product(self):
            return "Roamex"

        @property
        def base_bundle_id(self):
            return "com.roamex.Roamex"

    return RoamexCodeSignConfig


def roamex_get_parts(chromium_parts, ordered_keys):
    """Return an ordered list of part keys with the Sparkle parts injected
    immediately before the outer-app key (assumed last in `ordered_keys`).
    `chromium_parts` is Chromium's {key: CodeSignedProduct} dict (opaque here);
    we only order keys. The outer app stays last so its seal covers Sparkle."""
    keys = list(ordered_keys)
    if not keys:
        raise ValueError("no chromium parts to order")
    outer = keys[-1]
    sparkle_keys = [f"sparkle:{pathlib.PurePath(p).name}"
                    for p in SPARKLE_PART_PATHS]
    return keys[:-1] + sparkle_keys + [outer]

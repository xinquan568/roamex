# SPDX-License-Identifier: Apache-2.0
"""roam-33 / roam-97: the Roamux extension of Chromium's mac signing config.

Model B (roam-97). Chromium's `signing/config.py` couples two names:
  * `app_product` -> the OUTER bundle: `app_dir = '{.app_product}.app'`.
  * `product`     -> the NESTED parts: `framework_dir` and the helper apps are
    derived as `'{0.product} Framework.framework'` / `'{0.product} Helper*.app'`.
`RoamuxCodeSignConfig` therefore rebrands ONLY the outer app
(`app_product` -> "Roamux", `base_bundle_id` -> "com.roamux.Roamux") and
deliberately INHERITS `product` from the base (unbranded Chromium builds report
`product == "Chromium"`). This is the load-bearing Model-B decision: the nested
framework/helpers keep their on-disk "Chromium ..." names, exactly matching
`rename_bundle.py` (which renames only the outer `.app`, executable, and
Info.plist keys and leaves the nested Chromium helpers untouched). It avoids the
invasive Model-A surgery of renaming the inner framework/helpers and rewiring
every @rpath/@executable_path load command.

`roamux_get_parts()` injects the Sparkle framework + its nested code into
Chromium's ordered part keys BEFORE the outer app (which Chromium's pipeline
signs last, keeping the outer seal valid). Kept import-light so it is
unit-testable without a Chromium checkout on sys.path: the CodeSignConfig base
is imported lazily inside the factory.
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


def make_roamux_config_class(base_cls):
    """Given Chromium's CodeSignConfig, return a Roamux subclass (Model B).

    Only the OUTER app is rebranded: `app_product` -> "Roamux" and
    `base_bundle_id` -> "com.roamux.Roamux". `product` is intentionally NOT
    overridden — it is inherited from `base_cls` (unbranded Chromium builds
    report "Chromium"), so `config.framework_dir` and the helper-app part paths
    resolve to the nested "Chromium ..." bundles that `rename_bundle.py` leaves
    on disk. Split out so it can be unit-tested with a stub base (no Chromium
    checkout required)."""

    class RoamuxCodeSignConfig(base_cls):
        # NOTE (Model B): do NOT override `product`. Overriding it to "Roamux"
        # would make get_parts() look for a nonexistent "Roamux Framework.
        # framework"/"Roamux Helper*.app" (the nested parts keep Chromium
        # names). Only `app_product` (outer app) rebrands.
        @property
        def app_product(self):
            return "Roamux"

        @property
        def base_bundle_id(self):
            return "com.roamux.Roamux"

    return RoamuxCodeSignConfig


def sparkle_part_keys():
    """Stable keys for the Sparkle parts, deepest-first (framework last)."""
    return [f"sparkle:{pathlib.PurePath(p).name}" for p in SPARKLE_PART_PATHS]


def roamux_get_parts(chromium_parts, ordered_keys):
    """Return an ordered list of part keys with the Sparkle parts injected
    immediately before the outer-app key (assumed last in `ordered_keys`).
    `chromium_parts` is Chromium's {key: CodeSignedProduct} dict (opaque here);
    we only order keys. The outer app stays last so its seal covers Sparkle."""
    keys = list(ordered_keys)
    if not keys:
        raise ValueError("no chromium parts to order")
    outer = keys[-1]
    return keys[:-1] + sparkle_part_keys() + [outer]


def load_chromium_config_base(chromium_src):
    """Return Chromium's CodeSignConfig class from a checkout, or None if the
    signing package is not importable (i.e. not on the release builder)."""
    import importlib
    import sys
    mac = pathlib.Path(chromium_src) / "chrome" / "installer" / "mac"
    if not (mac / "signing" / "config_factory.py").is_file():
        return None
    if str(mac) not in sys.path:
        sys.path.insert(0, str(mac))
    try:
        cf = importlib.import_module("signing.config_factory")
        return cf.get_class()
    except Exception:
        return None

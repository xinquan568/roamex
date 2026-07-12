# SPDX-License-Identifier: Apache-2.0
"""Hermetic tests for check_app_icon.py (roam-103, TDD/P6 — written RED-first).

Self-contained by design: builds a fake overlay root (temporary expected payloads, passed via
--repo) and fixture .app bundle trees in tmp dirs. Never reads the production vendored assets, so
the suite stays independent of roamux/app/resources/icons/ content.

The invariant under test, for EVERY checked bundle (outer app + the Alerts helper when present):
  Contents/Resources/app.icns   byte-equal to <repo>/app/resources/icons/mac/app.icns
  Contents/Resources/Assets.car byte-equal to <repo>/app/resources/icons/mac/Assets.car
  Info.plist CFBundleIconFile == "app.icns" and CFBundleIconName == "AppIcon"
"""

import json
import pathlib
import plistlib
import subprocess
import sys
import tempfile
import unittest

REPO = pathlib.Path(__file__).resolve().parents[2]
CHECKER = REPO / "build" / "check_app_icon.py"

ICNS = b"roamux-bold-icns-payload"
CAR = b"roamux-bold-assets-car-payload"


def _write_plist(path, icon_file="app.icns", icon_name="AppIcon", drop=()):
    plist = {"CFBundleIconFile": icon_file, "CFBundleIconName": icon_name}
    for key in drop:
        plist.pop(key, None)
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "wb") as f:
        plistlib.dump(plist, f)


class AppIconCheckTest(unittest.TestCase):
    def setUp(self):
        # RED-first guard (roam-103): until check_app_icon.py lands, every test fails HERE, for
        # this one reason — never spuriously green via a nonzero exit from a missing script.
        self.assertTrue(CHECKER.exists(),
                        f"checker missing: {CHECKER} (RED state — implement it)")
        self._tmp = tempfile.TemporaryDirectory(prefix="roamux-icon-check-")
        self.tmp = pathlib.Path(self._tmp.name)
        # Fake overlay root: the checker resolves expected payloads under
        # <repo>/app/resources/icons/mac/.
        self.fake_repo = self.tmp / "roamux"
        icons = self.fake_repo / "app" / "resources" / "icons" / "mac"
        icons.mkdir(parents=True)
        (icons / "app.icns").write_bytes(ICNS)
        (icons / "Assets.car").write_bytes(CAR)

    def tearDown(self):
        self._tmp.cleanup()

    # ---- fixture builders -------------------------------------------------

    def _bundle(self, name="Fixture.app", icns=ICNS, car=CAR,
                icon_file="app.icns", icon_name="AppIcon", drop_keys=()):
        app = self.tmp / name
        res = app / "Contents" / "Resources"
        res.mkdir(parents=True)
        if icns is not None:
            (res / "app.icns").write_bytes(icns)
        if car is not None:
            (res / "Assets.car").write_bytes(car)
        _write_plist(app / "Contents" / "Info.plist",
                     icon_file=icon_file, icon_name=icon_name, drop=drop_keys)
        return app

    def _add_helper(self, app, icns=ICNS, car=CAR,
                    icon_file="app.icns", icon_name="AppIcon", drop_keys=()):
        helper = (app / "Contents" / "Frameworks" / "Fixture Framework.framework"
                  / "Versions" / "1.0" / "Helpers" / "Fixture Helper (Alerts).app")
        res = helper / "Contents" / "Resources"
        res.mkdir(parents=True)
        if icns is not None:
            (res / "app.icns").write_bytes(icns)
        if car is not None:
            (res / "Assets.car").write_bytes(car)
        _write_plist(helper / "Contents" / "Info.plist",
                     icon_file=icon_file, icon_name=icon_name, drop=drop_keys)
        return helper

    # ---- runner -----------------------------------------------------------

    def _run(self, app):
        return subprocess.run(
            [sys.executable, str(CHECKER),
             "--app", str(app), "--repo", str(self.fake_repo), "--json"],
            capture_output=True, text=True)

    def _summary(self, proc):
        # --json contract: stdout is EXACTLY one JSON document — nothing else.
        try:
            return json.loads(proc.stdout)
        except json.JSONDecodeError:
            self.fail(f"checker stdout is not pure JSON under --json; stdout={proc.stdout!r} "
                      f"stderr={proc.stderr!r}")

    # ---- outer-bundle invariant --------------------------------------------

    def test_all_good_outer_bundle_passes(self):
        proc = self._run(self._bundle())
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
        summary = self._summary(proc)
        self.assertTrue(summary["ok"])
        self.assertEqual(summary["bundles_checked"], 1)

    def test_missing_icns_fails(self):
        proc = self._run(self._bundle(icns=None))
        self.assertNotEqual(proc.returncode, 0)

    def test_mismatched_icns_fails(self):
        proc = self._run(self._bundle(icns=b"stock-chromium-icns"))
        self.assertNotEqual(proc.returncode, 0)

    def test_missing_car_fails(self):
        proc = self._run(self._bundle(car=None))
        self.assertNotEqual(proc.returncode, 0)

    def test_mismatched_car_fails(self):
        proc = self._run(self._bundle(car=b"stock-chromium-car"))
        self.assertNotEqual(proc.returncode, 0)

    def test_wrong_icon_file_key_fails(self):
        proc = self._run(self._bundle(icon_file="other.icns"))
        self.assertNotEqual(proc.returncode, 0)

    def test_missing_icon_file_key_fails(self):
        proc = self._run(self._bundle(drop_keys=("CFBundleIconFile",)))
        self.assertNotEqual(proc.returncode, 0)

    def test_wrong_icon_name_key_fails(self):
        proc = self._run(self._bundle(icon_name="OtherIcon"))
        self.assertNotEqual(proc.returncode, 0)

    def test_missing_icon_name_key_fails(self):
        proc = self._run(self._bundle(drop_keys=("CFBundleIconName",)))
        self.assertNotEqual(proc.returncode, 0)

    # ---- helper invariant (full, same as outer — plan finding 3) -----------

    def test_helper_absent_is_ok(self):
        proc = self._run(self._bundle())
        self.assertEqual(proc.returncode, 0)
        self.assertEqual(self._summary(proc)["bundles_checked"], 1)

    def test_all_good_with_helper_passes_and_counts_both(self):
        app = self._bundle()
        self._add_helper(app)
        proc = self._run(app)
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
        self.assertEqual(self._summary(proc)["bundles_checked"], 2)

    def test_helper_mismatched_payload_fails(self):
        app = self._bundle()
        self._add_helper(app, icns=b"stock-helper-icns")
        proc = self._run(app)
        self.assertNotEqual(proc.returncode, 0)

    def test_helper_missing_car_fails(self):
        app = self._bundle()
        self._add_helper(app, car=None)
        proc = self._run(app)
        self.assertNotEqual(proc.returncode, 0)

    def test_helper_missing_plist_icon_keys_fails(self):
        app = self._bundle()
        self._add_helper(app, drop_keys=("CFBundleIconFile", "CFBundleIconName"))
        proc = self._run(app)
        self.assertNotEqual(proc.returncode, 0)

    def test_helper_wrong_icon_name_key_fails(self):
        app = self._bundle()
        self._add_helper(app, icon_name="OtherIcon")
        proc = self._run(app)
        self.assertNotEqual(proc.returncode, 0)

    def test_helper_wrong_icon_file_key_alone_fails(self):
        app = self._bundle()
        self._add_helper(app, icon_file="other.icns")  # IconName stays correct
        proc = self._run(app)
        self.assertNotEqual(proc.returncode, 0)

    def test_helper_missing_icon_file_key_alone_fails(self):
        app = self._bundle()
        self._add_helper(app, drop_keys=("CFBundleIconFile",))  # IconName stays correct
        proc = self._run(app)
        self.assertNotEqual(proc.returncode, 0)

    # ---- summary shape ------------------------------------------------------

    def test_json_summary_shape_is_stable(self):
        proc = self._run(self._bundle())
        summary = self._summary(proc)
        self.assertEqual(sorted(summary), ["bundles_checked", "failures", "ok"])
        self.assertEqual(summary["failures"], [])


if __name__ == "__main__":
    unittest.main()

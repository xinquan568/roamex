# SPDX-License-Identifier: Apache-2.0
"""Hermetic guard over the committed Sparkle test-feed fixture (roam-101, TDD/P6 — RED-first).

Reads ONLY committed files under roamux/test/data/sparkle/ — no network, no Chromium checkout,
no vendored Sparkle. Enforces the roam-101 done criteria permanently:

  * the zip's TestHost Info.plist carries CFBundleIdentifier com.roamux.sparkle.testhost;
  * no `com.roamex` anywhere — plain fixture files as bytes AND every zip member DECOMPRESSED
    (raw deflate bytes are not a reliable inspection surface);
  * appcast.xml's edSignature verifies over the artifact with the committed public key, and its
    length attribute is byte-exact (appcast-unsigned.xml: no signature attribute, same length);
  * appcast-tampered.xml keeps its valid-key/WRONG-BYTES semantics: its signature verifies over
    the deterministic tampered bytes (artifact with byte 0 XOR 0xFF — exactly what
    regenerate_fixture.py signs) and does NOT verify over the real artifact — a garbage
    signature cannot pass;
  * regenerate_fixture.py contains no com.roamex literal.
"""

import base64
import pathlib
import plistlib
import re
import sys
import unittest
import xml.etree.ElementTree as ET
import zipfile

REPO_ROAMUX = pathlib.Path(__file__).resolve().parents[2]
FIXTURE_DIR = REPO_ROAMUX / "test" / "data" / "sparkle"
sys.path.insert(0, str(REPO_ROAMUX / "app" / "appcast"))
import ed25519_ref as ed  # noqa: E402  (pure Ed25519, test-only reference)

SPARKLE_NS = "http://www.andymatuschak.org/xml-namespaces/sparkle"
NEW_ID = "com.roamux.sparkle.testhost"
OLD_BRAND = b"com.roamex"


def _enclosure(appcast_path):
    root = ET.parse(appcast_path).getroot()
    enc = root.find(f".//enclosure")
    assert enc is not None, f"no enclosure in {appcast_path.name}"
    return enc


def _verify(message, sig_b64, pub_b64):
    return ed.verify(message, base64.b64decode(sig_b64), base64.b64decode(pub_b64))


class SparkleFixtureGuardTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.artifact = (FIXTURE_DIR / "Roamux-99.0.0.zip").read_bytes()
        cls.pub_b64 = (FIXTURE_DIR / "test_public_ed_key.b64").read_text().strip()

    def test_testhost_bundle_id_is_roamux(self):
        with zipfile.ZipFile(FIXTURE_DIR / "Roamux-99.0.0.zip") as z:
            plist = plistlib.loads(z.read("TestHost.app/Contents/Info.plist"))
        self.assertEqual(plist["CFBundleIdentifier"], NEW_ID)

    def test_no_old_brand_anywhere_in_fixture_dir(self):
        offenders = []
        for path in sorted(FIXTURE_DIR.iterdir()):
            data = path.read_bytes()
            if OLD_BRAND in data:
                offenders.append(f"{path.name} (raw bytes)")
            if path.suffix == ".zip":
                with zipfile.ZipFile(path) as z:
                    for member in z.namelist():
                        if OLD_BRAND in z.read(member):
                            offenders.append(f"{path.name}!{member} (decompressed)")
        self.assertEqual(offenders, [], f"old brand found in: {offenders}")

    def test_appcast_signature_verifies_and_length_exact(self):
        enc = _enclosure(FIXTURE_DIR / "appcast.xml")
        sig = enc.get(f"{{{SPARKLE_NS}}}edSignature")
        self.assertTrue(sig, "appcast.xml enclosure has no edSignature")
        self.assertTrue(_verify(self.artifact, sig, self.pub_b64),
                        "appcast.xml signature does not verify over the artifact")
        self.assertEqual(int(enc.get("length")), len(self.artifact))

    def test_unsigned_appcast_has_no_signature_but_exact_length(self):
        enc = _enclosure(FIXTURE_DIR / "appcast-unsigned.xml")
        self.assertIsNone(enc.get(f"{{{SPARKLE_NS}}}edSignature"))
        self.assertEqual(int(enc.get("length")), len(self.artifact))

    def test_tampered_appcast_keeps_valid_key_wrong_bytes_semantics(self):
        enc = _enclosure(FIXTURE_DIR / "appcast-tampered.xml")
        sig = enc.get(f"{{{SPARKLE_NS}}}edSignature")
        self.assertTrue(sig, "appcast-tampered.xml enclosure has no edSignature")
        # The generator signs the artifact with byte 0 flipped — the signature must be a REAL
        # signature over exactly those bytes (valid key, wrong bytes), not garbage...
        tampered = bytearray(self.artifact)
        tampered[0] ^= 0xFF
        self.assertTrue(_verify(bytes(tampered), sig, self.pub_b64),
                        "tampered signature is not a valid signature over the flipped bytes")
        # ...and it must NOT verify over the real artifact.
        self.assertFalse(_verify(self.artifact, sig, self.pub_b64),
                         "tampered signature unexpectedly verifies over the real artifact")
        self.assertEqual(int(enc.get("length")), len(self.artifact))

    def test_regenerate_script_has_no_old_brand_literal(self):
        script = (FIXTURE_DIR / "regenerate_fixture.py").read_bytes()
        self.assertNotIn(OLD_BRAND, script)


if __name__ == "__main__":
    unittest.main()

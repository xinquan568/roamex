# SPDX-License-Identifier: Apache-2.0
"""Hermetic tests for the roam-34 update leg — no production key, no gh, no
Sparkle keychain. Covers appcast generation + signature verification (dev
key), the sign_update output parser, the every-mode EdDSA key requirement
(K3), and the draft/publish invariants (K2)."""

import base64
import hashlib
import pathlib
import sys
import unittest
import xml.etree.ElementTree as ET

APPCAST = (pathlib.Path(__file__).resolve().parent.parent.parent / "app" /
           "appcast")
sys.path.insert(0, str(APPCAST))
import ed25519_ref as ed  # noqa: E402  (pure Ed25519 sign+verify, test only)
import generate_appcast  # noqa: E402
import release_flow  # noqa: E402
import sign_update_wrapper  # noqa: E402
import verify_appcast  # noqa: E402

TEST_SEED = bytes.fromhex(
    "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60")

SPARKLE_NS = "http://www.andymatuschak.org/xml-namespaces/sparkle"


class AppcastGenerationTest(unittest.TestCase):
    def setUp(self):
        self.pub = ed.publickey(TEST_SEED)
        self.pub_b64 = base64.b64encode(self.pub).decode()
        self.artifact = b"ROAMEX-UPDATE-99.0.0-universal2-" + b"z" * 200
        self.sig_b64 = base64.b64encode(
            ed.signature(self.artifact, TEST_SEED, self.pub)).decode()
        self.url = ("https://github.com/xinquan568/roamex/releases/download/"
                    "v99.0.0/Roamex.zip")

    def test_appcast_is_well_formed_and_signature_verifies(self):
        xml = generate_appcast.generate_appcast(
            version="99.0.0", enclosure_url=self.url,
            artifact_bytes=self.artifact, ed_signature=self.sig_b64,
            pub_date="Thu, 01 Jan 2026 00:00:00 +0000")
        root = ET.fromstring(xml)
        enc = root.find(".//enclosure")
        self.assertEqual(enc.get("url"), self.url)
        self.assertEqual(int(enc.get("length")), len(self.artifact))
        self.assertEqual(enc.get(f"{{{SPARKLE_NS}}}edSignature"), self.sig_b64)
        self.assertEqual(
            root.find(f".//{{{SPARKLE_NS}}}version").text, "99.0.0")
        # The committed signature verifies against the dev public key.
        self.assertTrue(_verify(self.artifact, self.sig_b64, self.pub_b64))

    def test_enclosure_url_is_artifact_not_feed(self):
        xml = generate_appcast.generate_appcast(
            version="99.0.0", enclosure_url=self.url,
            artifact_bytes=self.artifact, ed_signature=self.sig_b64,
            pub_date="Thu, 01 Jan 2026 00:00:00 +0000")
        url = ET.fromstring(xml).find(".//enclosure").get("url")
        self.assertTrue(url.endswith((".zip", ".dmg")), url)
        self.assertFalse(url.endswith("appcast.xml"), url)

    def test_length_is_byte_exact(self):
        xml = generate_appcast.generate_appcast(
            version="1.0", enclosure_url=self.url,
            artifact_bytes=b"abc", ed_signature=self.sig_b64,
            pub_date="x")
        self.assertEqual(
            int(ET.fromstring(xml).find(".//enclosure").get("length")), 3)


class VerifyAppcastCouplingTest(unittest.TestCase):
    def test_matching_key_passes(self):
        verify_appcast.assert_public_key_matches("ABC=", "ABC=")

    def test_mismatched_key_raises(self):
        # The signing key's public half must equal the committed SUPublicEDKey.
        with self.assertRaises(ValueError):
            verify_appcast.assert_public_key_matches("ABC=", "XYZ=")


class SignUpdateParserTest(unittest.TestCase):
    def test_parses_the_real_2_9_4_output_shape(self):
        line = ('sparkle:edSignature="EVX9lDb2cyDxWQg5BoOMWV1WxzS8bMVfyKSFQ'
                'zBPPr0hFBJruAkq5Y6sNoWjL60ROVzc4lQhahA0dmbjNMJpAA==" '
                'length="19"')
        got = sign_update_wrapper.parse_sign_update_output(line)
        self.assertEqual(got["length"], 19)
        self.assertTrue(got["signature"].endswith("=="))

    def test_malformed_output_raises(self):
        with self.assertRaises(ValueError):
            sign_update_wrapper.parse_sign_update_output("not a signature line")


class EveryModeKeyRequirementTest(unittest.TestCase):
    def test_present_key_returns_path(self):
        self.assertEqual(
            release_flow.require_sparkle_key({"SPARKLE_ED_PRIVATE_KEY": "k"}),
            "k")

    def test_missing_key_hard_fails_in_any_mode(self):
        # K3: no "publish an unsigned-by-Sparkle update" path.
        with self.assertRaises(release_flow.MissingSparkleKeyError):
            release_flow.require_sparkle_key({})


class DraftPublishInvariantTest(unittest.TestCase):
    def test_refuses_prerelease_publish(self):
        with self.assertRaises(release_flow.NotPublishableError):
            release_flow.assert_publishable(is_prerelease=True,
                                            staging_validated=True)

    def test_refuses_publish_before_staging_validation(self):
        with self.assertRaises(release_flow.NotPublishableError):
            release_flow.assert_publishable(is_prerelease=False,
                                            staging_validated=False)

    def test_allows_publish_when_release_and_validated(self):
        release_flow.assert_publishable(is_prerelease=False,
                                        staging_validated=True)

    def test_plan_release_urls(self):
        plan = release_flow.plan_release("v99.0.0", "Roamex.zip")
        self.assertIn("/releases/download/v99.0.0/appcast.xml",
                      plan["staging_feed_url"])
        self.assertTrue(plan["production_feed_url"].endswith(
            "/releases/latest/download/appcast.xml"))
        self.assertTrue(plan["enclosure_url"].endswith(
            "/releases/download/v99.0.0/Roamex.zip"))


def _verify(data, sig_b64, pub_b64):
    return ed.verify(data, base64.b64decode(sig_b64),
                     base64.b64decode(pub_b64))


if __name__ == "__main__":
    unittest.main()

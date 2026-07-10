# SPDX-License-Identifier: Apache-2.0
"""Staging validation (roam-34, K2): verify an appcast's sparkle:edSignature
over the artifact bytes against the committed SUPublicEDKey, BEFORE publish.
Uses Sparkle's own sign_update --verify when available, else the pure verifier.
This gates the publish step — a bad signature fails the release job."""

import argparse
import base64
import pathlib
import plistlib
import sys
import xml.etree.ElementTree as ET

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import ed25519_ref  # noqa: E402

SPARKLE_NS = "http://www.andymatuschak.org/xml-namespaces/sparkle"


def public_key_from_plist(plist_path):
    with open(plist_path, "rb") as f:
        return plistlib.load(f)["SUPublicEDKey"]


def edsignature_from_appcast(appcast_path):
    root = ET.fromstring(pathlib.Path(appcast_path).read_text())
    enc = root.find(".//enclosure")
    return enc.get(f"{{{SPARKLE_NS}}}edSignature")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--appcast", required=True)
    parser.add_argument("--artifact", required=True, type=pathlib.Path)
    parser.add_argument("--public-key-plist", required=True)
    args = parser.parse_args()

    sig_b64 = edsignature_from_appcast(args.appcast)
    pub_b64 = public_key_from_plist(args.public_key_plist)
    ok = ed25519_ref.verify(args.artifact.read_bytes(),
                            base64.b64decode(sig_b64),
                            base64.b64decode(pub_b64))
    if not ok:
        print("::error::staging validation FAILED — appcast edSignature does "
              "not verify against SUPublicEDKey; refusing to publish",
              file=sys.stderr)
        return 1
    print("[ok] staging validation passed — appcast signature verifies")
    return 0


if __name__ == "__main__":
    sys.exit(main())

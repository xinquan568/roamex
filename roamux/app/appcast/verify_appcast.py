# SPDX-License-Identifier: Apache-2.0
"""Staging validation (roam-34, K2): before publishing, prove the DRAFT
release's appcast verifies against the committed SUPublicEDKey using Sparkle's
OWN verifier (bin/sign_update --verify), not a test reference impl.

Two checks: (1) the keychain signing account's public key EQUALS the plist
SUPublicEDKey — so we are validating against the key the app actually trusts;
(2) `sign_update --verify <artifact> <edSignature> --account <acct>` passes on
the downloaded artifact bytes. A failure fails the release job before publish.
"""

import argparse
import pathlib
import plistlib
import subprocess
import sys
import xml.etree.ElementTree as ET

SPARKLE_NS = "http://www.andymatuschak.org/xml-namespaces/sparkle"


def public_key_from_plist(plist_path):
    with open(plist_path, "rb") as f:
        return plistlib.load(f)["SUPublicEDKey"]


def edsignature_from_appcast(appcast_path):
    root = ET.fromstring(pathlib.Path(appcast_path).read_text())
    return root.find(".//enclosure").get(f"{{{SPARKLE_NS}}}edSignature")


def assert_public_key_matches(plist_pub, account_pub):
    """The signing key's public half must equal the committed SUPublicEDKey —
    else the app (which trusts SUPublicEDKey) would reject the update."""
    if plist_pub.strip() != account_pub.strip():
        raise ValueError(
            "signing key public half does not match SUPublicEDKey: "
            f"plist={plist_pub!r} account={account_pub!r}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--appcast", required=True)
    parser.add_argument("--artifact", required=True, type=pathlib.Path)
    parser.add_argument("--public-key-plist", required=True)
    parser.add_argument("--sparkle-bin-dir", required=True, type=pathlib.Path)
    parser.add_argument("--account", required=True,
                        help="keychain account holding the signing key")
    args = parser.parse_args()

    plist_pub = public_key_from_plist(args.public_key_plist)
    account_pub = subprocess.run(
        [str(args.sparkle_bin_dir / "generate_keys"), "--account",
         args.account, "-p"], capture_output=True, text=True,
        check=True).stdout.strip()
    assert_public_key_matches(plist_pub, account_pub)

    sig = edsignature_from_appcast(args.appcast)
    result = subprocess.run(
        [str(args.sparkle_bin_dir / "sign_update"), "--verify",
         str(args.artifact), sig, "--account", args.account],
        capture_output=True, text=True)
    if result.returncode != 0:
        print("::error::staging validation FAILED — appcast edSignature does "
              f"not verify against SUPublicEDKey.\n{result.stderr}",
              file=sys.stderr)
        return 1
    print("[ok] staging validation passed — appcast verifies against "
          "SUPublicEDKey via Sparkle's verifier")
    return 0


if __name__ == "__main__":
    sys.exit(main())

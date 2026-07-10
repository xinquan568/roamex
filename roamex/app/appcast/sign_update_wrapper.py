# SPDX-License-Identifier: Apache-2.0
"""Wrap Sparkle's bin/sign_update (roam-34). It prints, for an artifact:
  sparkle:edSignature="<b64>" length="<bytes>"
This parses that into the enclosure fields; the release job runs the real tool
with the production private key file (materialized from SPARKLE_ED_PRIVATE_KEY).
"""

import argparse
import pathlib
import re
import subprocess
import sys

_RE = re.compile(r'sparkle:edSignature="([^"]+)"\s+length="(\d+)"')


def parse_sign_update_output(text):
    m = _RE.search(text)
    if not m:
        raise ValueError(f"unrecognized sign_update output: {text!r}")
    return {"signature": m.group(1), "length": int(m.group(2))}


def sign_update(sign_update_bin, artifact, key_file):
    result = subprocess.run(
        [str(sign_update_bin), str(artifact), "-f", str(key_file)],
        capture_output=True, text=True, check=True)
    return parse_sign_update_output(result.stdout)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sign-update-bin", required=True, type=pathlib.Path)
    parser.add_argument("--artifact", required=True, type=pathlib.Path)
    parser.add_argument("--key-file", required=True, type=pathlib.Path)
    args = parser.parse_args()
    got = sign_update(args.sign_update_bin, args.artifact, args.key_file)
    print(got["signature"])
    return 0


if __name__ == "__main__":
    sys.exit(main())

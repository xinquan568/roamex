# SPDX-License-Identifier: Apache-2.0
"""Generate a Sparkle appcast.xml for a Roamux release (roam-34, K2/K3).

One <item> with sparkle:version, pubDate, and an <enclosure> whose url is the
signed ARTIFACT download URL (…/releases/download/<tag>/<artifact>, never the
feed url), length is the exact artifact byte count, and sparkle:edSignature is
the EdDSA signature produced by Sparkle's sign_update (required in EVERY
signing mode). The in-app verifier (roam-32) checks the signature against
SUPublicEDKey.
"""

import argparse
import pathlib
import sys
from xml.sax.saxutils import quoteattr

TEMPLATE = """<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle">
  <channel>
    <title>Roamux</title>
    <item>
      <title>{version}</title>
      <sparkle:version>{version}</sparkle:version>
      <sparkle:minimumSystemVersion>12.0</sparkle:minimumSystemVersion>
      <pubDate>{pub_date}</pubDate>
      <enclosure url={url}
                 length="{length}"
                 type="application/octet-stream"
                 sparkle:edSignature={sig}/>
    </item>
  </channel>
</rss>
"""


def generate_appcast(version, enclosure_url, artifact_bytes, ed_signature,
                     pub_date):
    if enclosure_url.rstrip("/").endswith("appcast.xml"):
        raise ValueError("enclosure url must be the artifact, not the feed")
    return TEMPLATE.format(
        version=version, pub_date=pub_date,
        url=quoteattr(enclosure_url), length=len(artifact_bytes),
        sig=quoteattr(ed_signature))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--enclosure-url", required=True)
    parser.add_argument("--artifact", required=True, type=pathlib.Path)
    parser.add_argument("--ed-signature", required=True)
    parser.add_argument("--pub-date", required=True)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    args = parser.parse_args()
    xml = generate_appcast(args.version, args.enclosure_url,
                           args.artifact.read_bytes(), args.ed_signature,
                           args.pub_date)
    args.out.write_text(xml)
    print(f"[ok] appcast: {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

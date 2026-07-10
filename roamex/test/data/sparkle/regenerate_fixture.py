#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Regenerates the fixed pre-signed Sparkle test-feed fixture (roam-32).

TEST-ONLY key material: the Ed25519 seed below is a committed, clearly-marked
test fixture (plan §13.6/K3 — local/CI validation uses a dev/test pair; the
production key is minted by I-6.3 in the protected release environment and
never appears in this repo). Deterministic: same seed ⇒ same fixture bytes.

Uses the public-domain Ed25519 reference implementation (slow, fine for
fixtures) so regeneration needs no third-party Python packages.
"""

import base64
import hashlib
import pathlib

HERE = pathlib.Path(__file__).resolve().parent

# *** TEST KEY — NOT A SECRET. Do not use outside fixtures. ***
TEST_SEED = bytes.fromhex(
    "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60")

ARTIFACT_NAME = "Roamex-99.0.0.zip"
APPCAST_NAME = "appcast.xml"
APPCAST_TAMPERED_NAME = "appcast-tampered.xml"
APPCAST_UNSIGNED_NAME = "appcast-unsigned.xml"


# ---- Ed25519 reference implementation (public domain, ed25519.cr.yp.to) ----
b = 256
q = 2**255 - 19
l = 2**252 + 27742317777372353535851937790883648493


def H(m):
    return hashlib.sha512(m).digest()


def expmod(bb, e, m):
    return pow(bb, e, m)


def inv(x):
    return expmod(x, q - 2, q)


d = -121665 * inv(121666)
I = expmod(2, (q - 1) // 4, q)


def xrecover(y):
    xx = (y * y - 1) * inv(d * y * y + 1)
    x = expmod(xx, (q + 3) // 8, q)
    if (x * x - xx) % q != 0:
        x = (x * I) % q
    if x % 2 != 0:
        x = q - x
    return x


By = 4 * inv(5)
Bx = xrecover(By)
B = [Bx % q, By % q]


def edwards(P, Q):
    x1, y1 = P
    x2, y2 = Q
    x3 = (x1 * y2 + x2 * y1) * inv(1 + d * x1 * x2 * y1 * y2)
    y3 = (y1 * y2 + x1 * x2) * inv(1 - d * x1 * x2 * y1 * y2)
    return [x3 % q, y3 % q]


def scalarmult(P, e):
    if e == 0:
        return [0, 1]
    Q = scalarmult(P, e // 2)
    Q = edwards(Q, Q)
    if e & 1:
        Q = edwards(Q, P)
    return Q


def encodeint(y):
    return y.to_bytes(b // 8, "little")


def encodepoint(P):
    x, y = P
    bits = y | ((x & 1) << (b - 1))
    return bits.to_bytes(b // 8, "little")


def bit(h, i):
    return (h[i // 8] >> (i % 8)) & 1


def publickey(sk):
    h = H(sk)
    a = 2**(b - 2) + sum(2**i * bit(h, i) for i in range(3, b - 2))
    A = scalarmult(B, a)
    return encodepoint(A)


def Hint(m):
    h = H(m)
    return sum(2**i * bit(h, i) for i in range(2 * b))


def signature(m, sk, pk):
    h = H(sk)
    a = 2**(b - 2) + sum(2**i * bit(h, i) for i in range(3, b - 2))
    r = Hint(h[b // 8:b // 4] + m)
    R = scalarmult(B, r)
    S = (r + Hint(encodepoint(R) + pk + m) * a) % l
    return encodepoint(R) + encodeint(S)
# ---------------------------------------------------------------------------


def appcast(signature_b64, length, include_signature=True):
    sig_attr = (f' sparkle:edSignature="{signature_b64}"'
                if include_signature else "")
    return f"""<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle">
  <channel>
    <title>Roamex test feed (FIXTURE — dev/test key, roam-32)</title>
    <item>
      <title>99.0.0</title>
      <sparkle:version>99.0.0</sparkle:version>
      <sparkle:minimumSystemVersion>12.0</sparkle:minimumSystemVersion>
      <pubDate>Thu, 01 Jan 2026 00:00:00 +0000</pubDate>
      <enclosure url="https://example.invalid/{ARTIFACT_NAME}"
                 length="{length}"
                 type="application/octet-stream"{sig_attr}/>
    </item>
  </channel>
</rss>
"""


def main():
    pk = publickey(TEST_SEED)
    (HERE / "test_public_ed_key.b64").write_text(
        base64.b64encode(pk).decode() + "\n")

    # A REAL minimal .app zip: Sparkle validates the EdDSA signature after
    # extraction (before install), so extraction must succeed for the
    # signature gate to be reached. Deterministic (fixed timestamps).
    import io
    import zipfile
    plist = (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" '
        '"http://www.apple.com/DTDs/PropertyList-1.0.dtd">\n'
        '<plist version="1.0"><dict>\n'
        '  <key>CFBundleIdentifier</key><string>com.roamex.sparkle.testhost</string>\n'
        '  <key>CFBundleName</key><string>TestHost</string>\n'
        '  <key>CFBundleExecutable</key><string>TestHost</string>\n'
        '  <key>CFBundleVersion</key><string>99.0.0</string>\n'
        '  <key>CFBundleShortVersionString</key><string>99.0.0</string>\n'
        # Sparkle only supports EdDSA key rotation, never removal: the new
        # app must carry a key too (same test key).
        f'  <key>SUPublicEDKey</key><string>{base64.b64encode(pk).decode()}</string>\n'
        '</dict></plist>\n')
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
        def add(name, data, mode=0o644):
            info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            info.external_attr = (0o100000 | mode) << 16
            z.writestr(info, data)
        add("TestHost.app/Contents/Info.plist", plist)
        add("TestHost.app/Contents/MacOS/TestHost", "#!/bin/sh\nexit 0\n",
            mode=0o755)
    artifact = buf.getvalue()
    (HERE / ARTIFACT_NAME).write_bytes(artifact)

    sig = signature(artifact, TEST_SEED, pk)
    sig_b64 = base64.b64encode(sig).decode()
    (HERE / "artifact_signature.b64").write_text(sig_b64 + "\n")

    (HERE / APPCAST_NAME).write_text(appcast(sig_b64, len(artifact)))
    # Tampered: the signature is over DIFFERENT bytes than the artifact.
    tampered = bytearray(artifact)
    tampered[0] ^= 0xFF
    tam_sig = base64.b64encode(
        signature(bytes(tampered), TEST_SEED, pk)).decode()
    (HERE / APPCAST_TAMPERED_NAME).write_text(
        appcast(tam_sig, len(artifact)))
    (HERE / APPCAST_UNSIGNED_NAME).write_text(
        appcast("", len(artifact), include_signature=False))
    print("fixture regenerated (test key, deterministic)")


if __name__ == "__main__":
    main()

# SPDX-License-Identifier: Apache-2.0
"""Pure-Python Ed25519 (public-domain ed25519.cr.yp.to reference impl) — sign
AND verify — for HERMETIC TESTS ONLY (roam-34). Slow; never used in
production (the app verifies via BoringSSL, roam-32; the release job signs via
Sparkle's sign_update). Lets the appcast round-trip test prove a generated
signature verifies against the public key with no keychain/native dep."""

import hashlib

b = 256
q = 2 ** 255 - 19
l = 2 ** 252 + 27742317777372353535851937790883648493


def H(m):
    return hashlib.sha512(m).digest()


def inv(x):
    return pow(x, q - 2, q)


d = -121665 * inv(121666) % q
I = pow(2, (q - 1) // 4, q)


def xrecover(y):
    xx = (y * y - 1) * inv(d * y * y + 1)
    x = pow(xx, (q + 3) // 8, q)
    if (x * x - xx) % q != 0:
        x = (x * I) % q
    if x % 2 != 0:
        x = q - x
    return x


By = 4 * inv(5) % q
Bx = xrecover(By)
B = [Bx % q, By % q]


def edwards(P, Q):
    x1, y1, x2, y2 = P[0], P[1], Q[0], Q[1]
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
    return (P[1] | ((P[0] & 1) << (b - 1))).to_bytes(b // 8, "little")


def bit(h, i):
    return (h[i // 8] >> (i % 8)) & 1


def publickey(sk):
    h = H(sk)
    a = 2 ** (b - 2) + sum(2 ** i * bit(h, i) for i in range(3, b - 2))
    return encodepoint(scalarmult(B, a))


def Hint(m):
    h = H(m)
    return sum(2 ** i * bit(h, i) for i in range(2 * b))


def signature(m, sk, pk):
    h = H(sk)
    a = 2 ** (b - 2) + sum(2 ** i * bit(h, i) for i in range(3, b - 2))
    r = Hint(h[b // 8:b // 4] + m)
    R = scalarmult(B, r)
    S = (r + Hint(encodepoint(R) + pk + m) * a) % l
    return encodepoint(R) + encodeint(S)


def isoncurve(P):
    x, y = P
    return (-x * x + y * y - 1 - d * x * x * y * y) % q == 0


def decodeint(s):
    return int.from_bytes(s, "little")


def decodepoint(s):
    y = int.from_bytes(s, "little") & ((1 << (b - 1)) - 1)
    x = xrecover(y)
    if x & 1 != bit(s, b - 1):
        x = q - x
    P = [x, y]
    if not isoncurve(P):
        raise ValueError("decoding point that is not on curve")
    return P


def verify(m, s, pk):
    """True iff signature `s` (64 bytes) over `m` verifies with `pk` (32)."""
    if len(s) != b // 4 or len(pk) != b // 8:
        return False
    try:
        R = decodepoint(s[:b // 8])
        A = decodepoint(pk)
    except ValueError:
        return False
    S = decodeint(s[b // 8:b // 4])
    h = Hint(encodepoint(R) + pk + m)
    return scalarmult(B, S) == edwards(R, scalarmult(A, h))

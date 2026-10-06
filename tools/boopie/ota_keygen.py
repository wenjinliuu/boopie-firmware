#!/usr/bin/env python3
# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0

"""Makes the OTA release key, once, on your own computer. Plain Python, no packages.

  python3 tools/boopie/ota_keygen.py

writes boopie-ota-key.pem in the current folder: the private key. Put its whole
text in the repository's GitHub secret BOOPIE_OTA_KEY (Settings > Secrets and
variables > Actions), keep the file somewhere safe and offline, and never commit
it or paste it anywhere else. The release workflow signs each update with it and
builds the matching public key into the firmware, so a board only installs
updates signed with this key. Lose it and boards flashed with its public key
can't be updated over the air any more (a cable flash still works).
"""

from __future__ import annotations

import base64
import os
import secrets
import sys

# NIST P-256
P = 0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF
A = P - 3
N = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551
G = (0x6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296,
     0x4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5)


def add(p, q):
    if p is None:
        return q
    if q is None:
        return p
    if p[0] == q[0] and (p[1] + q[1]) % P == 0:
        return None
    if p == q:
        m = (3 * p[0] * p[0] + A) * pow(2 * p[1], -1, P) % P
    else:
        m = (q[1] - p[1]) * pow(q[0] - p[0], -1, P) % P
    x = (m * m - p[0] - q[0]) % P
    return x, (m * (p[0] - x) - p[1]) % P


def mul(k, p):
    r = None
    while k:
        if k & 1:
            r = add(r, p)
        p = add(p, p)
        k >>= 1
    return r


def der(tag: int, body: bytes) -> bytes:
    n = len(body)
    size = bytes([n]) if n < 0x80 else bytes([0x81, n]) if n < 0x100 else bytes([0x82, n >> 8, n & 0xFF])
    return bytes([tag]) + size + body


def main() -> None:
    out = "boopie-ota-key.pem"
    if os.path.exists(out):
        sys.exit(f"{out} is already here; move it away first (a new key can't update boards made with the old one)")
    d = secrets.randbelow(N - 1) + 1
    x, y = mul(d, G)
    point = b"\x04" + x.to_bytes(32, "big") + y.to_bytes(32, "big")
    key = der(0x30, der(0x02, b"\x01") + der(0x04, d.to_bytes(32, "big"))
              + der(0xA0, der(0x06, bytes.fromhex("2a8648ce3d030107")))   # prime256v1
              + der(0xA1, der(0x03, b"\x00" + point)))
    b64 = base64.encodebytes(key).decode().replace("\n", "")
    pem = "-----BEGIN EC PRIVATE KEY-----\n" + "\n".join(b64[i:i + 64] for i in range(0, len(b64), 64)) \
        + "\n-----END EC PRIVATE KEY-----\n"
    fd = os.open(out, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "w") as f:
        f.write(pem)
    print(f"wrote {out} (the private key: GitHub secret BOOPIE_OTA_KEY only)")
    print("public key (fine to share; the release workflow works it out by itself):")
    print(point.hex())


if __name__ == "__main__":
    main()

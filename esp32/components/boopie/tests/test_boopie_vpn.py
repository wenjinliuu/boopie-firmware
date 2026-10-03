# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0

"""Boopie's VPN: Shadowsocks AEAD and 2022 (vpn/boopie_ss.c) against a
reference written here from the specs with Python's cryptography, both ways,
every cipher; BLAKE3 (vpn/boopie_blake3.c) against the official
implementation's output; and reading subscriptions (vpn/boopie_vpn_nodes.c):
ss:// links old and new, base64 subscriptions and Clash YAML.

Run from esp32/: python3 -m unittest discover -s components/boopie/tests -p 'test_*.py'
"""

from __future__ import annotations

import base64
import hashlib
import struct
import time
import json
import os
import shlex
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
COMPONENT = HERE.parent

try:
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM, ChaCha20Poly1305
    from cryptography.hazmat.primitives.kdf.hkdf import HKDF
except ImportError:   # pragma: no cover
    AESGCM = None

CIPHERS = {"aes-128-gcm": 16, "aes-192-gcm": 24, "aes-256-gcm": 32, "chacha20-ietf-poly1305": 32}
CIPHERS_2022 = {"2022-blake3-aes-128-gcm": 16, "2022-blake3-aes-256-gcm": 32, "2022-blake3-chacha20-poly1305": 32}

# BLAKE3 of bytes(i % 251 for i in range(n)), from the official implementation
# (pip's blake3, 1.0); and two derive_key outputs, the second cut to 16 bytes.
BLAKE3_VECTORS = [
    (0, "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262"),
    (1, "2d3adedff11b61f14c886e35afa036736dcd87a74d27b5c1510225d0f592e213"),
    (63, "e9bc37a594daad83be9470df7f7b3798297c3d834ce80ba85d6e207627b7db7b"),
    (64, "4eed7141ea4a5cd4b788606bd23f46e212af9cacebacdc7d1f4c6dc7f2511b98"),
    (65, "de1e5fa0be70df6d2be8fffd0e99ceaa8eb6e8c93a63f2d8d1c30ecb6b263dee"),
    (128, "f17e570564b26578c33bb7f44643f539624b05df1a76c81f30acd548c44b45ef"),
    (1023, "10108970eeda3eb932baac1428c7a2163b0e924c9a9e25b35bba72b28f70bd11"),
    (1024, "42214739f095a406f3fc83deb889744ac00df831c10daa55189b5d121c855af7"),
]
BLAKE3_SESSION_64 = "374fca03e4dae7f998fd7e59c1edfcc8e3197f4db1c19ca1671be3b66a92ddda"
BLAKE3_IDENTITY_48_16 = "d03d862b5351843498b5fe4be23a7477"


def pattern(n: int) -> bytes:
    return bytes(i % 251 for i in range(n))


# ---- BLAKE3 in Python, one chunk (as much as 2022 needs), for the reference ----

_IV = [0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A, 0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19]
_PERM = [2, 6, 3, 10, 7, 0, 4, 13, 1, 11, 12, 5, 9, 14, 15, 8]
_M32 = 0xFFFFFFFF


def _compress(cv: list[int], block: bytes, block_len: int, flags: int) -> list[int]:
    m = list(struct.unpack("<16I", block))
    s = cv[:] + _IV[:4] + [0, 0, block_len, flags]

    def g(a: int, b: int, c: int, d: int, x: int, y: int) -> None:
        s[a] = (s[a] + s[b] + x) & _M32
        s[d] = ((s[d] ^ s[a]) >> 16 | (s[d] ^ s[a]) << 16) & _M32
        s[c] = (s[c] + s[d]) & _M32
        s[b] = ((s[b] ^ s[c]) >> 12 | (s[b] ^ s[c]) << 20) & _M32
        s[a] = (s[a] + s[b] + y) & _M32
        s[d] = ((s[d] ^ s[a]) >> 8 | (s[d] ^ s[a]) << 24) & _M32
        s[c] = (s[c] + s[d]) & _M32
        s[b] = ((s[b] ^ s[c]) >> 7 | (s[b] ^ s[c]) << 25) & _M32

    for r in range(7):
        g(0, 4, 8, 12, m[0], m[1]); g(1, 5, 9, 13, m[2], m[3]); g(2, 6, 10, 14, m[4], m[5])
        g(3, 7, 11, 15, m[6], m[7]); g(0, 5, 10, 15, m[8], m[9]); g(1, 6, 11, 12, m[10], m[11])
        g(2, 7, 8, 13, m[12], m[13]); g(3, 4, 9, 14, m[14], m[15])
        m = [m[i] for i in _PERM]
    return [s[i] ^ s[i + 8] for i in range(8)] + [s[i + 8] ^ cv[i] for i in range(8)]


def _chunk(key: list[int], flags: int, data: bytes, out_len: int) -> bytes:
    assert len(data) <= 1024
    blocks = [data[i:i + 64] for i in range(0, len(data), 64)] or [b""]
    cv = key
    for i, b in enumerate(blocks):
        f = flags | (1 if i == 0 else 0)
        if i == len(blocks) - 1:
            w = _compress(cv, b.ljust(64, b"\0"), len(b), f | 2 | 8)
            return struct.pack("<16I", *w)[:out_len]
        cv = _compress(cv, b, 64, f)[:8]
    raise AssertionError


def py_blake3(data: bytes, out_len: int = 32) -> bytes:
    return _chunk(_IV, 0, data, out_len)


def py_derive_key(context: str, material: bytes, out_len: int) -> bytes:
    ck = _chunk(_IV, 32, context.encode(), 32)
    return _chunk(list(struct.unpack("<8I", ck)), 64, material, out_len)


def bytes_to_key(password: bytes, n: int) -> bytes:
    out, d = b"", b""
    while len(out) < n:
        d = hashlib.md5(d + password).digest()
        out += d
    return out[:n]


class Ref:
    """One direction of a Shadowsocks AEAD stream."""

    def __init__(self, cipher: str, password: str, salt: bytes) -> None:
        n = CIPHERS[cipher]
        sub = HKDF(algorithm=hashes.SHA1(), length=n, salt=salt, info=b"ss-subkey").derive(
            bytes_to_key(password.encode(), n))
        self.aead = ChaCha20Poly1305(sub) if cipher.startswith("chacha") else AESGCM(sub)
        self.counter = 0

    def _nonce(self) -> bytes:
        n = self.counter.to_bytes(12, "little")
        self.counter += 1
        return n

    def seal(self, data: bytes) -> bytes:
        out = b""
        for i in range(0, len(data), 0x3FFF):
            part = data[i:i + 0x3FFF]
            out += self.aead.encrypt(self._nonce(), len(part).to_bytes(2, "big"), None)
            out += self.aead.encrypt(self._nonce(), part, None)
        return out

    def open(self, stream: bytes) -> bytes:
        out, i = b"", 0
        while i < len(stream):
            n = int.from_bytes(self.aead.decrypt(self._nonce(), stream[i:i + 18], None), "big")
            i += 18
            out += self.aead.decrypt(self._nonce(), stream[i:i + n + 16], None)
            i += n + 16
        return out


class Ref2022:
    """One direction of a Shadowsocks 2022 stream (SIP022), the server's view."""

    def __init__(self, cipher: str, psk: bytes, salt: bytes) -> None:
        sub = py_derive_key("shadowsocks 2022 session subkey", psk + salt, len(psk))
        self.aead = ChaCha20Poly1305(sub) if "chacha" in cipher else AESGCM(sub)
        self.counter = 0

    def _nonce(self) -> bytes:
        n = self.counter.to_bytes(12, "little")
        self.counter += 1
        return n

    def seal(self, data: bytes) -> bytes:
        return self.aead.encrypt(self._nonce(), data, None)

    def open(self, data: bytes) -> bytes:
        return self.aead.decrypt(self._nonce(), data, None)


def read_request_2022(cipher: str, psks: list[bytes], stream: bytes) -> tuple[int, str, int, bytes]:
    """A server reading a client's 2022 request: (time, host, port, all its payload)."""
    n = len(psks[0])
    salt, i = stream[:n], n
    # Identity headers: each key in the chain names the next.
    for k in range(len(psks) - 1):
        sub = py_derive_key("shadowsocks 2022 identity subkey", psks[k] + salt, n)
        enc = Cipher(algorithms.AES(sub), modes.ECB()).encryptor()
        assert stream[i:i + 16] == enc.update(py_blake3(psks[k + 1], 16)) + enc.finalize()
        i += 16
    r = Ref2022(cipher, psks[-1], salt)
    fixed = r.open(stream[i:i + 11 + 16])
    i += 27
    assert fixed[0] == 0
    ts, vlen = struct.unpack(">QH", fixed[1:])
    var = r.open(stream[i:i + vlen + 16])
    i += vlen + 16
    assert var[0] == 3
    hl = var[1]
    host, port = var[2:2 + hl].decode(), struct.unpack(">H", var[2 + hl:4 + hl])[0]
    pad = struct.unpack(">H", var[4 + hl:6 + hl])[0]
    payload = var[6 + hl + pad:]
    assert (pad >= 1) if not payload else True
    while i < len(stream):
        ln = struct.unpack(">H", r.open(stream[i:i + 18]))[0]
        i += 18
        payload += r.open(stream[i:i + ln + 16])
        i += ln + 16
    return ts, host, port, payload


def reply_2022(cipher: str, psk: bytes, request_salt: bytes, data: bytes, ts: int) -> bytes:
    """A server's 2022 reply: its salt, the header (echoing the request's salt), then chunks."""
    salt = os.urandom(len(psk))
    r = Ref2022(cipher, psk, salt)
    first, rest = data[:0xFFFF], data[0xFFFF:]
    out = salt + r.seal(b"\x01" + struct.pack(">Q", ts) + request_salt + struct.pack(">H", len(first)))
    out += r.seal(first)
    for i in range(0, len(rest), 0xFFFF):
        part = rest[i:i + 0xFFFF]
        out += r.seal(struct.pack(">H", len(part))) + r.seal(part)
    return out


def mbedcrypto_flags() -> list[str]:
    if shutil.which("pkg-config"):
        r = subprocess.run(["pkg-config", "--cflags", "--libs", "mbedcrypto"], capture_output=True, text=True)
        if r.returncode == 0:
            return shlex.split(r.stdout)
    return ["-lmbedcrypto"]


@unittest.skipIf(AESGCM is None, "needs Python's cryptography")
class BoopieVpnTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = Path(cls.tmp.name) / "boopie_vpn_harness"
        cc = shlex.split(os.environ.get("CC", "cc"))
        r = subprocess.run(cc + ["-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(COMPONENT / "vpn"),
                                 str(HERE / "boopie_vpn_harness.c"), str(COMPONENT / "vpn" / "boopie_ss.c"), str(COMPONENT / "vpn" / "boopie_blake3.c"),
                                 str(COMPONENT / "vpn" / "boopie_vpn_nodes.c"), *mbedcrypto_flags(),
                                 "-o", str(cls.exe)], capture_output=True, text=True)
        if r.returncode:
            if "psa/crypto.h" in r.stderr:
                raise unittest.SkipTest("needs mbedtls's PSA Crypto (libmbedtls-dev)")
            raise AssertionError(r.stderr)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.tmp.cleanup()

    def run_h(self, *args: str, stdin: bytes = b"") -> subprocess.CompletedProcess:
        return subprocess.run([str(self.exe), *args], input=stdin, capture_output=True)

    def test_our_streams_open_with_the_reference(self) -> None:
        data = os.urandom(40000) + "你好，布比".encode()
        for cipher, n in CIPHERS.items():
            with self.subTest(cipher=cipher):
                salt = os.urandom(n)
                r = self.run_h("seal", cipher, "pa55word", salt.hex(), stdin=data)
                self.assertEqual(r.returncode, 0)
                stream = bytes.fromhex(r.stdout.decode())
                self.assertEqual(stream[:n], salt)
                self.assertEqual(Ref(cipher, "pa55word", salt).open(stream[n:]), data)

    def test_reference_streams_open_with_ours_in_any_pieces(self) -> None:
        data = os.urandom(50000)
        for cipher, n in CIPHERS.items():
            salt = os.urandom(n)
            stream = salt + Ref(cipher, "密码 pass", salt).seal(data)
            for step in (1, 7, 18, 1000, 70000):
                with self.subTest(cipher=cipher, step=step):
                    r = self.run_h("open", cipher, "密码 pass", str(step), stdin=stream.hex().encode())
                    self.assertEqual(r.returncode, 0)
                    self.assertEqual(r.stdout, data)

    def test_wrong_password_or_tampering_is_caught(self) -> None:
        salt = os.urandom(32)
        stream = bytearray(salt + Ref("aes-256-gcm", "right", salt).seal(b"hello" * 100))
        self.assertEqual(self.run_h("open", "aes-256-gcm", "wrong", "100", stdin=bytes(stream).hex().encode()).returncode, 3)
        stream[60] ^= 1
        self.assertEqual(self.run_h("open", "aes-256-gcm", "right", "100", stdin=bytes(stream).hex().encode()).returncode, 3)

    def test_address_is_a_domain(self) -> None:
        r = self.run_h("address", "hatch.metaaivm.com", "443")
        self.assertEqual(bytes.fromhex(r.stdout.decode()), b"\x03\x12hatch.metaaivm.com\x01\xbb")

    def test_blake3_matches_the_official_one(self) -> None:
        for n, want in BLAKE3_VECTORS:
            with self.subTest(n=n):
                self.assertEqual(self.run_h("blake3", pattern(n).hex()).stdout.decode(), want)
                self.assertEqual(py_blake3(pattern(n)).hex(), want)
        self.assertEqual(self.run_h("blake3", pattern(64).hex(), "shadowsocks 2022 session subkey").stdout.decode(),
                         BLAKE3_SESSION_64)
        self.assertEqual(py_derive_key("shadowsocks 2022 session subkey", pattern(64), 32).hex(), BLAKE3_SESSION_64)
        self.assertEqual(py_derive_key("shadowsocks 2022 identity subkey", pattern(48), 16).hex(), BLAKE3_IDENTITY_48_16)

    def test_2022_requests_read_by_the_reference(self) -> None:
        data = os.urandom(30000)
        now = int(time.time())
        for cipher, n in CIPHERS_2022.items():
            chains = [[os.urandom(n)]] + ([[os.urandom(n), os.urandom(n)], [os.urandom(n) for _ in range(3)]]
                                          if "aes" in cipher else [])
            for psks in chains:
                for payload in (data, b"\x16\x03\x01hello", b""):
                    with self.subTest(cipher=cipher, keys=len(psks), payload=len(payload)):
                        password = ":".join(base64.b64encode(k).decode() for k in psks)
                        salt = os.urandom(n)
                        r = self.run_h("req2022", cipher, password, salt.hex(), str(now), "api.muse.ai", "443",
                                       stdin=payload)
                        self.assertEqual(r.returncode, 0, r.stderr)
                        ts, host, port, got = read_request_2022(cipher, psks, bytes.fromhex(r.stdout.decode()))
                        self.assertEqual((ts, host, port, got), (now, "api.muse.ai", 443, payload))

    def test_2022_replies_open_with_ours(self) -> None:
        data = os.urandom(150000)   # past one 64 KB chunk
        now = int(time.time())
        for cipher, n in CIPHERS_2022.items():
            psk = os.urandom(n)
            password = base64.b64encode(psk).decode()
            req_salt = os.urandom(n)
            stream = reply_2022(cipher, psk, req_salt, data, now - 3)
            for step in (1, 59, 4096, 200000):
                with self.subTest(cipher=cipher, step=step):
                    r = self.run_h("open2022", cipher, password, req_salt.hex(), str(now), str(step),
                                   stdin=stream.hex().encode())
                    self.assertEqual(r.returncode, 0)
                    self.assertEqual(r.stdout, data)

    def test_2022_refuses_a_reply_not_to_us_or_stale(self) -> None:
        psk = os.urandom(32)
        password = base64.b64encode(psk).decode()
        req_salt = os.urandom(32)
        now = int(time.time())
        other = reply_2022("2022-blake3-aes-256-gcm", psk, os.urandom(32), b"hi", now)
        stale = reply_2022("2022-blake3-aes-256-gcm", psk, req_salt, b"hi", now - 120)
        for stream in (other, stale):
            r = self.run_h("open2022", "2022-blake3-aes-256-gcm", password, req_salt.hex(), str(now), "64",
                           stdin=stream.hex().encode())
            self.assertEqual(r.returncode, 3)

    def parse(self, text: str) -> list[dict]:
        out = self.run_h("parse", stdin=text.encode()).stdout.decode()
        return [json.loads(line) for line in out.splitlines()]

    def test_sip002_and_legacy_links(self) -> None:
        user = base64.urlsafe_b64encode(b"chacha20-ietf-poly1305:p@ss:word").decode().rstrip("=")
        legacy = base64.b64encode(b"aes-256-gcm:secret@1.2.3.4:8388").decode()
        key32 = base64.b64encode(bytes(range(32))).decode().replace("+", "%2B").replace("/", "%2F").replace("=", "%3D")
        nodes = self.parse(f"ss://{user}@example.com:443/?type=x#%E9%A6%99%E6%B8%AF%2001\n"
                           f"ss://{legacy}#Legacy\n"
                           "ss://2022-blake3-aes-128-gcm:abc%3D@[2001:db8::1]:9000#SS2022\n"
                           f"ss://2022-blake3-aes-256-gcm:{key32}@h2.example:443#Good2022\n"
                           "ss://YWVzLTEyOC1nY206cHc@h.example:1?plugin=obfs-local#Plugin\n"
                           "vmess://whatever\n")
        self.assertEqual(len(nodes), 4)
        self.assertEqual(nodes[0], {"name": "香港 01", "host": "example.com", "port": 443,
                                    "cipher": "chacha20-ietf-poly1305", "password": "p@ss:word", "supported": True})
        self.assertEqual((nodes[1]["name"], nodes[1]["host"], nodes[1]["port"], nodes[1]["password"]),
                         ("Legacy", "1.2.3.4", 8388, "secret"))
        self.assertEqual((nodes[2]["host"], nodes[2]["cipher"], nodes[2]["password"], nodes[2]["supported"]),
                         ("2001:db8::1", "2022-blake3-aes-128-gcm", "abc=", False))   # not a 16-byte key
        self.assertEqual((nodes[3]["name"], nodes[3]["supported"]), ("Good2022", True))

    def test_base64_subscription(self) -> None:
        links = "\n".join(
            f"ss://{base64.urlsafe_b64encode(f'aes-128-gcm:pw{i}'.encode()).decode()}@n{i}.example:80{i}#Node{i}"
            for i in range(5))
        nodes = self.parse(base64.b64encode(links.encode()).decode())
        self.assertEqual([n["name"] for n in nodes], [f"Node{i}" for i in range(5)])
        self.assertEqual(nodes[3]["port"], 803)

    def test_clash_yaml(self) -> None:
        yaml = """port: 7890
proxies:
  - {name: "日本 1", type: ss, server: jp.example, port: 443, cipher: aes-256-gcm, password: "a,b"}
  - name: 美国
    type: ss
    server: us.example
    port: 8388
    cipher: chacha20-ietf-poly1305
    password: 'xyz'
  - {name: vm, type: vmess, server: v.example, port: 1, uuid: x}
  - name: obfs
    type: ss
    server: o.example
    port: 2
    cipher: aes-128-gcm
    password: p
    plugin: obfs
proxy-groups:
  - name: auto
"""
        nodes = self.parse(yaml)
        self.assertEqual([(n["name"], n["host"], n["port"], n["password"]) for n in nodes],
                         [("日本 1", "jp.example", 443, "a,b"), ("美国", "us.example", 8388, "xyz")])


if __name__ == "__main__":
    unittest.main()

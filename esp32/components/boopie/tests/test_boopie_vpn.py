# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0

"""Boopie's VPN: Shadowsocks AEAD (vpn/boopie_ss.c) against a reference
written here from the spec with Python's cryptography, both ways, every
cipher; and reading subscriptions (vpn/boopie_vpn_nodes.c): ss:// links old
and new, base64 subscriptions and Clash YAML.

Run from esp32/: python3 -m unittest discover -s components/boopie/tests -p 'test_*.py'
"""

from __future__ import annotations

import base64
import hashlib
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
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM, ChaCha20Poly1305
    from cryptography.hazmat.primitives.kdf.hkdf import HKDF
except ImportError:   # pragma: no cover
    AESGCM = None

CIPHERS = {"aes-128-gcm": 16, "aes-192-gcm": 24, "aes-256-gcm": 32, "chacha20-ietf-poly1305": 32}


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
                                 str(HERE / "boopie_vpn_harness.c"), str(COMPONENT / "vpn" / "boopie_ss.c"),
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

    def parse(self, text: str) -> list[dict]:
        out = self.run_h("parse", stdin=text.encode()).stdout.decode()
        return [json.loads(line) for line in out.splitlines()]

    def test_sip002_and_legacy_links(self) -> None:
        user = base64.urlsafe_b64encode(b"chacha20-ietf-poly1305:p@ss:word").decode().rstrip("=")
        legacy = base64.b64encode(b"aes-256-gcm:secret@1.2.3.4:8388").decode()
        nodes = self.parse(f"ss://{user}@example.com:443/?type=x#%E9%A6%99%E6%B8%AF%2001\n"
                           f"ss://{legacy}#Legacy\n"
                           "ss://2022-blake3-aes-128-gcm:abc%3D@[2001:db8::1]:9000#SS2022\n"
                           "ss://YWVzLTEyOC1nY206cHc@h.example:1?plugin=obfs-local#Plugin\n"
                           "vmess://whatever\n")
        self.assertEqual(len(nodes), 3)
        self.assertEqual(nodes[0], {"name": "香港 01", "host": "example.com", "port": 443,
                                    "cipher": "chacha20-ietf-poly1305", "password": "p@ss:word", "supported": True})
        self.assertEqual((nodes[1]["name"], nodes[1]["host"], nodes[1]["port"], nodes[1]["password"]),
                         ("Legacy", "1.2.3.4", 8388, "secret"))
        self.assertEqual((nodes[2]["host"], nodes[2]["cipher"], nodes[2]["password"], nodes[2]["supported"]),
                         ("2001:db8::1", "2022-blake3-aes-128-gcm", "abc=", False))

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

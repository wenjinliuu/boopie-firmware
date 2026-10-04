# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0

"""小智's check-in (xiaozhi/boopie_xz_proto.c, docs/boopie-xiaozhi.md): what
the board says about itself, as itself so the server never offers it the
official firmware, and what it reads from the answer: an activation code
while unbound, where to talk once bound, the time; the firmware offered is
only noted, never taken.

Run from esp32/: python3 -m unittest discover -s components/boopie/tests -p 'test_*.py'
"""

from __future__ import annotations

import json
import os
import re
import shlex
import subprocess
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
COMPONENT = HERE.parent
CJSON = COMPONENT.parent.parent / "managed_components" / "espressif__cjson" / "cJSON"


@unittest.skipUnless((CJSON / "cJSON.c").exists(), "cJSON isn't fetched (idf.py reconfigure)")
class BoopieXiaozhiTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = Path(cls.tmp.name) / "boopie_xz_harness"
        cc = shlex.split(os.environ.get("CC", "cc"))
        subprocess.run(cc + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-Wno-error=format-truncation",
                             "-I", str(COMPONENT / "xiaozhi"), "-I", str(CJSON),
                             str(HERE / "boopie_xz_harness.c"), str(COMPONENT / "xiaozhi" / "boopie_xz_proto.c"),
                             str(CJSON / "cJSON.c"), "-lm", "-o", str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.tmp.cleanup()

    def run_x(self, *args: str, stdin: str = ""):
        r = subprocess.run([str(self.exe), *args], input=stdin, capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        return json.loads(r.stdout)

    def parse(self, answer) -> dict:
        return self.run_x("parse", stdin=json.dumps(answer) if not isinstance(answer, str) else answer)

    def test_it_checks_in_as_itself(self) -> None:
        r = self.run_x("info")
        body = r["body"]
        self.assertEqual(r["small"], -1)                       # too small a buffer: refused, not cut
        self.assertEqual(body["mac_address"], "aa:bb:cc:dd:ee:ff")
        self.assertEqual(body["uuid"], "12345678-1234-4123-8123-123456789abc")
        self.assertEqual(body["application"], {"name": "boopie", "version": "1.2.3"})
        self.assertEqual(body["board"]["type"], "boopie")    # not an official board's name
        self.assertEqual(body["language"], "zh-CN")

    def test_a_uuid_v4(self) -> None:
        u = self.run_x("uuid")
        self.assertRegex(u, r"^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$")

    def test_unbound_it_gets_a_code(self) -> None:
        r = self.parse({"activation": {"code": "123456", "message": "xiaozhi.me\n123456", "challenge": "abc-123",
                                       "timeout_ms": 30000},
                        "server_time": {"timestamp": 1790000000000, "timezone_offset": 480},
                        "firmware": {"version": "9.9.9", "url": "https://example.com/xz.bin"}})
        self.assertEqual((r["ok"], r["has_code"], r["code"], r["has_challenge"], r["timeout_ms"]),
                         (1, 1, "123456", 1, 30000))
        self.assertEqual(r["has_websocket"], 0)
        self.assertEqual((r["has_time"], r["time_ms"]), (1, 1790000000000))   # UTC: the offset isn't added
        self.assertEqual(r["firmware"], 1)                     # noted, never taken

    def test_bound_it_gets_where_to_talk(self) -> None:
        r = self.parse({"websocket": {"url": "wss://api.tenclass.net/xiaozhi/v1/", "token": "t" * 100},
                        "server_time": {"timestamp": 1790000000000}})
        self.assertEqual((r["has_code"], r["has_challenge"]), (0, 0))
        self.assertEqual((r["has_websocket"], r["ws_url"], r["token_len"]), (1, "wss://api.tenclass.net/xiaozhi/v1/", 100))
        self.assertEqual(r["firmware"], 0)

    def test_odd_answers(self) -> None:
        self.assertEqual(self.parse("not json")["ok"], 0)
        self.assertEqual(self.parse("[1, 2]")["ok"], 0)
        r = self.parse({"websocket": {"token": "only"}, "server_time": {"timestamp": 5}})
        self.assertEqual((r["ok"], r["has_websocket"], r["has_time"]), (1, 0, 0))   # no url; an absurd time
        r = self.parse({"activation": {"code": "1" * 200}})
        self.assertEqual(len(r["code"]), 15)                   # kept to its buffer


if __name__ == "__main__":
    unittest.main()

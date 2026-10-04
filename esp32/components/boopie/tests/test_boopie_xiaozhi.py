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

    def test_the_conversation_messages(self) -> None:
        hello = self.run_x("hello")
        self.assertEqual(hello["type"], "hello")
        self.assertEqual(hello["features"], {"mcp": True})
        self.assertEqual(hello["audio_params"], {"format": "opus", "sample_rate": 16000, "channels": 1,
                                                 "frame_duration": 60})
        listen, abort = self.run_x("listen", 'a"b', "start")
        self.assertEqual(listen, {"session_id": 'a"b', "type": "listen", "state": "start", "mode": "manual"})
        self.assertEqual(abort, {"session_id": 'a"b', "type": "abort"})

    def msg(self, m) -> dict:
        return self.run_x("msg", stdin=json.dumps(m) if not isinstance(m, str) else m)

    def test_reading_the_server(self) -> None:
        r = self.msg({"type": "hello", "transport": "websocket", "session_id": "xyz",
                      "audio_params": {"format": "opus", "sample_rate": 24000, "channels": 1, "frame_duration": 60}})
        self.assertEqual((r["type"], r["session"], r["rate"], r["frame"]), (1, "xyz", 24000, 60))
        self.assertEqual(self.msg({"type": "stt", "text": "你好"})["type"], 2)
        self.assertEqual(self.msg({"type": "stt", "text": "你好"})["text"], "你好")
        self.assertEqual(self.msg({"type": "llm", "emotion": "happy", "text": "😊"})["text"], "happy")
        self.assertEqual(self.msg({"type": "tts", "state": "start"})["type"], 4)
        r = self.msg({"type": "tts", "state": "sentence_start", "text": "我在呢"})
        self.assertEqual((r["type"], r["text"]), (5, "我在呢"))
        self.assertEqual(self.msg({"type": "tts", "state": "stop"})["type"], 6)
        self.assertEqual(self.msg({"type": "mcp", "payload": {}})["type"], 7)
        self.assertEqual(self.msg({"type": "system", "command": "reboot"})["type"], 0)   # never obeyed
        self.assertEqual(self.msg("garbage")["ok"], 0)

    def mcp(self, payload, tools: str = "[]") -> dict:
        return self.run_x("mcp", tools, stdin=json.dumps({"session_id": "s1", "type": "mcp", "payload": payload}))

    def test_mcp(self) -> None:
        r = self.mcp({"jsonrpc": "2.0", "method": "initialize", "id": 1, "params": {}})
        self.assertEqual(r["out"]["type"], "mcp")
        res = r["out"]["payload"]
        self.assertEqual((res["id"], res["result"]["serverInfo"]["name"]), (1, "boopie"))
        self.assertIn("tools", res["result"]["capabilities"])
        r = self.mcp({"jsonrpc": "2.0", "method": "tools/list", "id": 2}, '[{"name":"self.x"}]')
        self.assertEqual(r["out"]["payload"]["result"]["tools"], [{"name": "self.x"}])
        r = self.mcp({"jsonrpc": "2.0", "method": "tools/list", "id": 3}, "not json")
        self.assertEqual(r["out"]["payload"]["result"]["tools"], [])
        r = self.mcp({"jsonrpc": "2.0", "method": "tools/call", "id": 4,
                      "params": {"name": "self.audio_speaker.set_volume", "arguments": {"volume": 50}}})
        self.assertEqual((r["r"], r["name"], r["args"], r["id"]), (-2, "self.audio_speaker.set_volume", {"volume": 50}, 4))
        self.assertEqual(r["result"]["payload"]["result"]["content"], [{"type": "text", "text": "好的"}])
        r = self.mcp({"jsonrpc": "2.0", "method": "nope", "id": 5})
        self.assertEqual(r["out"]["payload"]["error"]["code"], -32601)
        self.assertEqual(self.mcp({"jsonrpc": "2.0", "method": "notifications/initialized"})["r"], -1)

    def test_moods(self) -> None:
        self.assertEqual(self.run_x("mood", "happy", "laughing", "crying", "sleepy", "shocked", "neutral", ""),
                         [1, 1, 2, 3, 4, 0, 0])


if __name__ == "__main__":
    unittest.main()

# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0

"""Chat history, the album and notes waiting to be sent (store/boopie_history.c):
each keeps to its cap, newest first, and survives what it holds.

Run from esp32/: python3 -m unittest discover -s components/boopie/tests -p 'test_*.py'
"""

from __future__ import annotations

import json
import os
import shlex
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
COMPONENT = HERE.parent


class BoopieHistoryTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = Path(cls.tmp.name) / "boopie_history_harness"
        cc = shlex.split(os.environ.get("CC", "cc"))
        subprocess.run(cc + ["-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(COMPONENT / "store"),
                             str(HERE / "boopie_history_harness.c"), str(COMPONENT / "store" / "boopie_history.c"),
                             str(COMPONENT / "store" / "boopie_store.c"), "-o", str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.tmp.cleanup()

    def setUp(self) -> None:
        self.data = tempfile.TemporaryDirectory()

    def tearDown(self) -> None:
        self.data.cleanup()

    def run_h(self, *args: str) -> str:
        r = subprocess.run([str(self.exe), *args], env=dict(os.environ, BOOPIE_DATA=self.data.name),
                           capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        return r.stdout

    def chat(self) -> list[dict]:
        return [json.loads(line) for line in self.run_h("chat").splitlines()]

    def test_chat_keeps_the_last_100_newest_first(self) -> None:
        self.run_h("chat-add", "130")
        turns = self.chat()
        self.assertEqual(len(turns), 100)
        self.assertEqual(turns[0], {"when": 1700000129, "said": "said 129", "reply": "reply 129\nline two\t!"})
        self.assertEqual(turns[-1]["said"], "said 30")
        lines = (Path(self.data.name) / "chat" / "log.txt").read_text().count("\n")
        self.assertLessEqual(lines, 120)   # trimmed as it goes

    def test_chat_text_round_trips(self) -> None:
        said, reply = "你好，布比\\不是\t制表", "今天晴，25℃。\n第二行"
        self.run_h("chat-add-text", said, reply)
        self.assertEqual(self.chat()[0]["said"], said)
        self.assertEqual(self.chat()[0]["reply"], reply)

    def test_long_chinese_is_cut_on_a_character(self) -> None:
        self.run_h("chat-add-text", "说" * 200, "好" * 600)
        turn = self.chat()[0]
        self.assertTrue(turn["said"] and set(turn["said"]) == {"说"})
        self.assertTrue(set(turn["reply"]) == {"好"})

    def test_album_keeps_10(self) -> None:
        self.run_h("album-add", "13")
        self.run_h("album-broken")
        self.assertEqual(self.run_h("album").splitlines(), [f"jpeg {i}" for i in range(12, 2, -1)])
        self.assertFalse((Path(self.data.name) / "album" / ".incoming").exists())

    def test_notes_save_list_and_drop(self) -> None:
        a = self.run_h("notes-add", "16000").strip()
        b = self.run_h("notes-add", "500").strip()
        self.assertLess(a, b)
        self.assertEqual(self.run_h("notes").splitlines(), [f"{a} 16000 ok", f"{b} 500 ok"])
        self.run_h("notes-drop", a)
        self.assertEqual(self.run_h("notes").splitlines(), [f"{b} 500 ok"])

    def test_notes_older_than_a_week_go(self) -> None:
        notes = Path(self.data.name) / "notes"
        self.run_h("notes")   # makes the folders
        old = f"{int(time.time()) - 8 * 24 * 3600:010d}.pcm"
        (notes / old).write_bytes(b"\0\0" * 10)
        fresh = self.run_h("notes-add", "10").strip()
        self.assertEqual(self.run_h("notes").splitlines(), [f"{fresh} 10 ok"])
        self.assertFalse((notes / old).exists())


if __name__ == "__main__":
    unittest.main()

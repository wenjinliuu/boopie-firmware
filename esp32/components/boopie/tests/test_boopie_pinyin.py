# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0

"""The nine-key pinyin input's matching (ime/boopie_pinyin.c) and dictionary."""

from __future__ import annotations

import json
import os
import shlex
import subprocess
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
COMPONENT = HERE.parent
KEYS = ["64", "94664", "2", "24", "242", "99999", "2426"]


class BoopiePinyinTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "boopie_pinyin_harness"
            cc = shlex.split(os.environ.get("CC", "cc"))
            subprocess.run(cc + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(COMPONENT / "ime"),
                                 str(HERE / "boopie_pinyin_harness.c"), str(COMPONENT / "ime" / "boopie_pinyin.c"),
                                 str(COMPONENT / "ime" / "boopie_pinyin_dict.c"), "-o", str(exe)], check=True)
            cls.r = json.loads(subprocess.run([str(exe), *KEYS], check=True, capture_output=True,
                                              text=True).stdout)

    def test_every_syllable(self) -> None:
        self.assertGreater(self.r["count"], 400)

    def test_whole_spellings_first_then_longer_ones(self) -> None:
        py = self.r["64"]["py"]
        self.assertEqual(py[:3], ["ni", "mi", "ng"])   # 6-4 spells these whole (ng: 嗯)
        self.assertTrue(all(len(p) > 2 and p[:2] in ("ni", "mi") for p in py[3:]))

    def test_the_likelier_spelling_first(self) -> None:
        self.assertEqual(self.r["94664"]["py"][:2], ["zhong", "xiong"])

    def test_common_characters_first(self) -> None:
        self.assertTrue(self.r["64"]["first"].startswith("你"))
        self.assertTrue(self.r["94664"]["first"].startswith("中"))
        self.assertEqual(set(self.r["2426"]["py"][:3]), {"bian", "biao", "chan"})   # o and n share 6

    def test_nothing_spelled(self) -> None:
        self.assertEqual(self.r["99999"]["py"], [])


if __name__ == "__main__":
    unittest.main()

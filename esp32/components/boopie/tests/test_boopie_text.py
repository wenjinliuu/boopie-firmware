# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0

"""Chinese and English text layout: column widths (boopie_text.c), the UI
font's measurements (font/boopie_ui_metrics.c), and how replies wrap into
screen pages (muse_chat_text.c's next_line, which uses both).

Run from esp32/: python3 -m unittest discover -s components/boopie/tests -p 'test_*.py'
"""

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
ESP32 = COMPONENT.parent.parent
MUSE = ESP32 / "components" / "muse"


class BoopieTextTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = Path(cls.tmp.name) / "boopie_text_harness"
        cc = shlex.split(os.environ.get("CC", "cc"))
        subprocess.run(
            cc + ["-include", str(ESP32 / "tests" / "host_compat.h"),
                  "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                  "-I", str(COMPONENT), "-I", str(COMPONENT / "font"), "-I", str(MUSE),
                  str(HERE / "boopie_text_harness.c"),
                  str(COMPONENT / "boopie_text.c"),
                  str(COMPONENT / "font" / "boopie_ui_metrics.c"),
                  str(COMPONENT / "font" / "boopie_ui_metrics_data.c"),
                  str(MUSE / "muse_chat_text.c"), str(MUSE / "muse_text.c"),
                  "-o", str(cls.exe)],
            check=True)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.tmp.cleanup()

    def run_harness(self, *args: str, stdin: str = "") -> str:
        return subprocess.run([str(self.exe), *args], input=stdin.encode(), check=True,
                              capture_output=True).stdout.decode()

    def page(self, text: str, cols: int, lines: int = 20, px: int | None = None) -> list[str]:
        extra = [str(px)] if px else []
        return self.run_harness("page", str(cols), str(lines), *extra, stdin=text).split("\n")

    def test_columns(self) -> None:
        self.assertEqual(self.run_harness("cols", stdin="abc"), "3")
        self.assertEqual(self.run_harness("cols", stdin="中文abc"), "7")
        self.assertEqual(self.run_harness("cols", stdin="你好，世界！"), "12")
        self.assertEqual(self.run_harness("cols", stdin="25℃"), "4")

    def test_font_tables(self) -> None:
        stats = json.loads(self.run_harness("font"))
        self.assertEqual(stats["bad"], 0)
        self.assertGreater(stats["glyphs"], 7700)
        # Chinese characters and full-width marks are an em wide.
        self.assertGreater(stats["wide"], 7000)
        self.assertGreater(stats["em"], stats["wide"] - 40)

    def test_tables_match_the_font(self) -> None:
        try:
            from fontTools.ttLib import TTFont
        except ImportError:
            self.skipTest("needs fontTools")
        font = TTFont(COMPONENT / "font" / "ui.otf")
        cmap, hmtx = font.getBestCmap(), font["hmtx"].metrics
        ours = dict(tuple(map(int, line.split())) for line in self.run_harness("advances").splitlines())
        self.assertEqual(ours, {cp: hmtx[g][0] for cp, g in cmap.items() if cp <= 0xFFFF})

    def test_font_covers_everyday_text(self) -> None:
        text = ("今天天气很好，我们去公园散步吧！温度25℃，湿度60%。"
                "俊券即势刹咨“你好”《布比》……①②③"
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
                "āáǎàōóǒòēéěèīíǐìūúǔùǖǘǚǜü"
                "，。、；：？！（）【】「」『』—")
        self.assertEqual(self.run_harness("has", text), "")
        # Every one of GB2312's 3755 level-1 (most used) hanzi.
        level1 = "".join(bytes([b1, b2]).decode("gb2312")
                         for b1 in range(0xB0, 0xD8) for b2 in range(0xA1, 0xFF)
                         if (b1, b2) < (0xD7, 0xFA))
        self.assertEqual(len(level1), 3755)
        self.assertEqual(self.run_harness("has", level1), "")

    def test_chinese_breaks_between_characters(self) -> None:
        self.assertEqual(self.page("今天天气很好我们去公园散步吧", 16),
                         ["今天天气很好我们", "去公园散步吧"])

    def test_closing_punctuation_never_starts_a_line(self) -> None:
        self.assertEqual(self.page("你好世界。再见", 8), ["你好世", "界。再见"])
        self.assertEqual(self.page("我们，你们", 4), ["我", "们，", "你们"])

    def test_opening_punctuation_never_ends_a_line(self) -> None:
        self.assertEqual(self.page("你说：《布比》", 8), ["你说：", "《布比》"])

    def test_mixed_chinese_and_english(self) -> None:
        self.assertEqual(self.page("Hello world 你好世界", 16), ["Hello world 你好", "世界"])
        self.assertEqual(self.page("我会说English和中文", 12), ["我会说", "English和中", "文"])

    def test_measured_lines_fit(self) -> None:
        # The reply caption: 22 px Noto, 11 px columns.
        text = "今天天气很好，我们去公园散步吧！The quick brown fox jumps over the lazy dog. 布比说：“你好”。"
        lines = self.page(text, 20, 40, px=22)
        self.assertGreater(len(lines), 2)
        for line in lines:
            self.assertLessEqual(int(self.run_harness("width", "22", stdin=line)), 220, line)
        self.assertEqual("".join(lines).replace(" ", ""), text.replace(" ", ""))
        # A Chinese character is a full em: ten of them fill 20 columns.
        self.assertEqual(self.page("一二三四五六七八九十百千", 20, px=22), ["一二三四五六七八九十", "百千"])
        # Narrow Latin letters fit more to a line than wide ones.
        narrow = self.page("i" * 60, 20, px=22)[0]
        wide = self.page("W" * 60, 20, px=22)[0]
        self.assertGreater(len(narrow), 2 * len(wide))

    def test_english_unchanged(self) -> None:
        self.assertEqual(self.page("the quick brown fox jumps", 10),
                         ["the quick", "brown fox", "jumps"])
        self.assertEqual(self.page("supercalifragilistic", 8), ["supercal", "ifragili", "stic"])


if __name__ == "__main__":
    unittest.main()

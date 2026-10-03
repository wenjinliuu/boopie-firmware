# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0

"""The assets pack: tools/boopie/pack_assets.py writes it, store/boopie_assets.c
reads it, and a damaged or half-written one isn't used."""

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
PACKER = COMPONENT.parents[2] / "tools" / "boopie" / "pack_assets.py"


class BoopieAssetsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.tmp = tempfile.TemporaryDirectory()
        tmp = Path(cls.tmp.name)
        cls.exe = tmp / "boopie_assets_harness"
        cc = shlex.split(os.environ.get("CC", "cc"))
        subprocess.run(cc + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(COMPONENT / "store"),
                             str(HERE / "boopie_assets_harness.c"), str(COMPONENT / "store" / "boopie_assets.c"),
                             "-o", str(cls.exe)], check=True)
        src = tmp / "assets"
        (src / "fonts").mkdir(parents=True)
        (src / "VERSION").write_text("7\n")
        (src / "README.md").write_text("left out")
        (src / "fonts" / "ui.ttf").write_text("FONTDATA")
        (src / "beep.wav").write_text("BEEP")
        cls.pack = tmp / "assets.bin"
        subprocess.run(["python3", str(PACKER), "--dir", str(src), "--out", str(cls.pack)], check=True,
                       capture_output=True)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.tmp.cleanup()

    def read(self, pack: Path | None, *names: str) -> dict:
        env = dict(os.environ)
        env.pop("BOOPIE_ASSETS", None)
        if pack:
            env["BOOPIE_ASSETS"] = str(pack)
        out = subprocess.run([str(self.exe), *names], env=env, check=True, capture_output=True, text=True).stdout
        return json.loads(out)

    def test_reads_what_was_packed(self) -> None:
        r = self.read(self.pack, "fonts/ui.ttf", "beep.wav", "README.md", "nope")
        self.assertEqual(r, {"ready": 1, "version": 7, "fonts/ui.ttf": "FONTDATA", "beep.wav": "BEEP",
                             "README.md": None, "nope": None})

    def test_no_pack(self) -> None:
        self.assertEqual(self.read(None, "beep.wav"), {"ready": 0, "version": 0, "beep.wav": None})

    def test_damaged_tables_arent_used(self) -> None:
        data = bytearray(self.pack.read_bytes())
        data[40] ^= 0xFF   # in the first entry's name: its CRC no longer matches
        bad = Path(self.tmp.name) / "bad.bin"
        bad.write_bytes(data)
        self.assertEqual(self.read(bad, "beep.wav")["ready"], 0)

    def test_half_written_isnt_used(self) -> None:
        half = Path(self.tmp.name) / "half.bin"
        half.write_bytes(self.pack.read_bytes()[:60])
        self.assertEqual(self.read(half, "beep.wav")["ready"], 0)
        blank = Path(self.tmp.name) / "blank.bin"
        blank.write_bytes(b"\xff" * 4096)   # erased flash, never written
        self.assertEqual(self.read(blank, "beep.wav")["ready"], 0)


if __name__ == "__main__":
    unittest.main()

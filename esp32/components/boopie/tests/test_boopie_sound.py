# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0

"""Boopie's sounds (sound/boopie_sound.c): each plays, none clips or clicks,
and a theme's WAV in the assets pack takes over."""

from __future__ import annotations

import json
import os
import shlex
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
COMPONENT = HERE.parent
PACKER = COMPONENT.parents[2] / "tools" / "boopie" / "pack_assets.py"


def wav(frames: list[int]) -> bytes:
    data = struct.pack(f"<{len(frames)}h", *frames)
    return (b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, 1, 16000, 32000, 2, 16)
            + b"data" + struct.pack("<I", len(data)) + data)


class BoopieSoundTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.tmp = tempfile.TemporaryDirectory()
        tmp = Path(cls.tmp.name)
        cls.exe = tmp / "boopie_sound_harness"
        cc = shlex.split(os.environ.get("CC", "cc"))
        subprocess.run(cc + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(COMPONENT / "sound"),
                             "-I", str(COMPONENT / "store"), str(HERE / "boopie_sound_harness.c"),
                             str(COMPONENT / "sound" / "boopie_sound.c"), str(COMPONENT / "store" / "boopie_assets.c"),
                             "-lm", "-o", str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.tmp.cleanup()

    def run_it(self, assets: Path | None = None) -> dict:
        env = dict(os.environ)
        env.pop("BOOPIE_ASSETS", None)
        if assets:
            env["BOOPIE_ASSETS"] = str(assets)
        return json.loads(subprocess.run([str(self.exe)], env=env, check=True, capture_output=True, text=True).stdout)

    def test_every_sound_plays_without_clipping_or_clicking(self) -> None:
        r = self.run_it()
        for key in ("boot", "off", "listen", "sent", "error", "level_up", "eat", "poke", "score", "gold", "cloud",
                    "game_over", "notify", "purr", "hello"):
            frames, peak, last = r[key]
            with self.subTest(sound=key):
                self.assertGreater(frames, 16000 * 0.05)    # at least 50 ms
                self.assertLess(frames, 16000 * 1.5)
                self.assertGreater(peak, 2000)              # heard
                self.assertLess(peak, 12000)                # with room, never clipping
                self.assertLess(last, 500)                  # fades out, no click at the end

    def test_one_waits_and_a_newer_replaces_it(self) -> None:
        self.assertEqual(self.run_it()["queue"], [1, 1, 0])

    def test_a_themes_wav_takes_over(self) -> None:
        src = Path(self.tmp.name) / "assets"
        (src / "sounds").mkdir(parents=True)
        (src / "VERSION").write_text("1\n")
        (src / "sounds" / "poke.wav").write_bytes(wav([1000, -1000] * 400))
        pack = Path(self.tmp.name) / "assets.bin"
        subprocess.run(["python3", str(PACKER), "--dir", str(src), "--out", str(pack)], check=True, capture_output=True)
        r = self.run_it(pack)
        self.assertEqual(r["poke"][:2], [800, 1000])
        self.assertEqual(r["eat"], self.run_it()["eat"])   # the rest still synthesized


if __name__ == "__main__":
    unittest.main()

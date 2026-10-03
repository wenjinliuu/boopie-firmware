# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0

"""白噪音 (sound/boopie_noise.c): each sound at a sane level without
clipping, the right colour (pink and the waves darker than white, rain with
bright drops), fading in, out at the end of its time, and quickly on a stop.

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

WHITE, PINK, RAIN, WAVES = range(4)


class BoopieNoiseTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = Path(cls.tmp.name) / "boopie_noise_harness"
        cc = shlex.split(os.environ.get("CC", "cc"))
        subprocess.run(cc + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(COMPONENT / "sound"),
                             str(HERE / "boopie_noise_harness.c"), str(COMPONENT / "sound" / "boopie_noise.c"),
                             "-lm", "-o", str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.tmp.cleanup()

    def run_h(self, *args: object):
        r = subprocess.run([str(self.exe), *map(str, args)], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        return json.loads(r.stdout)

    def test_levels_and_colours(self) -> None:
        s = {k: self.run_h("stats", k, 30) for k in (WHITE, PINK, RAIN, WAVES)}
        for k, v in s.items():
            self.assertEqual(v["clipped"], 0, k)
            self.assertGreater(v["rms"], 700, k)    # audible ...
            self.assertLess(v["rms"], 2600, k)      # ... and under a voice
        # White is bright; pink, rain and the waves are darker, the waves the darkest.
        self.assertGreater(s[WHITE]["high"], 1.5)
        for k in (PINK, RAIN, WAVES):
            self.assertGreater(s[k]["low"], 5 * s[WHITE]["low"], k)
            self.assertLess(s[k]["high"], s[WHITE]["high"] / 3, k)
        self.assertLess(s[WAVES]["high"], s[PINK]["high"] / 4)
        # Rain's drops: bright clicks over a pink hiss.
        self.assertGreater(s[RAIN]["high"], s[PINK]["high"])

    def test_fades_in_and_out_at_the_end_of_its_time(self) -> None:
        t = self.run_h("timeline", PINK, 1)
        level = [x[0] for x in t]
        steady = sum(level[5:45]) / 40
        self.assertLess(level[0], steady * 0.6)               # fading in
        self.assertGreater(level[3], steady * 0.8)
        self.assertGreater(level[49], steady * 0.8)           # 10 s from the end
        self.assertLess(level[57], steady * 0.4)              # fading out
        self.assertEqual(t[44][1:], [1, 16])                  # playing, 15.001 s left, rounded up
        self.assertEqual(level[61:], [0.0] * len(level[61:]))
        self.assertEqual(t[61][1], 0)                         # stopped

    def test_a_stop_fades_quickly(self) -> None:
        t = self.run_h("timeline", WHITE, 0, 20)
        self.assertEqual(t[10][1:], [1, -1])                  # until stopped
        self.assertGreater(t[19][0], 1000)
        self.assertEqual(t[23][0], 0.0)
        self.assertEqual(t[23][1], 0)


if __name__ == "__main__":
    unittest.main()

# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0

"""The pet's world through the year (world/boopie_season.c): festivals by the
date, the lunar ones from a table for 2026 to 2035, and each day's weather by
the season, the same all day.

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

NONE, SPRING, MOON, HALLOWEEN, XMAS, NEW_YEAR, VALENTINE, DRAGON, CHILDREN, BIRTHDAY = range(10)
SUNNY, CLOUDY, RAIN, SNOW = range(4)


class BoopieSeasonTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = Path(cls.tmp.name) / "boopie_season_harness"
        cc = shlex.split(os.environ.get("CC", "cc"))
        subprocess.run(cc + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(COMPONENT / "world"),
                             str(HERE / "boopie_season_harness.c"), str(COMPONENT / "world" / "boopie_season.c"),
                             "-o", str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.tmp.cleanup()

    def run_s(self, *steps: str):
        r = subprocess.run([str(self.exe), *steps], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        return [json.loads(line) for line in r.stdout.splitlines()]

    def fest(self, y: int, m: int, d: int) -> int:
        return self.run_s(f"fest:{y}:{m}:{d}")[0]

    def test_spring_festival_from_its_eve_for_a_week(self) -> None:
        # 2026: 17 February; 2028: 26 January.
        self.assertEqual([self.fest(2026, 2, d) for d in (15, 16, 17, 23, 24)], [NONE, SPRING, SPRING, SPRING, NONE])
        self.assertEqual([self.fest(2028, 1, d) for d in (25, 26)], [SPRING, SPRING])
        self.assertEqual(self.fest(2036, 1, 28), NONE)        # past the table: none

    def test_mid_autumn_the_day_either_side(self) -> None:
        # 2026: 25 September; 2028: 3 October.
        self.assertEqual([self.fest(2026, 9, d) for d in (23, 24, 25, 26, 27)], [NONE, MOON, MOON, MOON, NONE])
        self.assertEqual(self.fest(2028, 10, 4), MOON)

    def test_the_fixed_ones(self) -> None:
        self.assertEqual([self.fest(2026, 10, d) for d in (27, 28, 31)], [NONE, HALLOWEEN, HALLOWEEN])
        self.assertEqual([self.fest(2026, 12, d) for d in (19, 20, 25, 26, 27, 31)],
                         [NONE, XMAS, XMAS, XMAS, NONE, NEW_YEAR])
        self.assertEqual([self.fest(2027, 1, d) for d in (1, 2)], [NEW_YEAR, NONE])

    def test_valentines_dragon_boat_and_childrens_day(self) -> None:
        self.assertEqual([self.fest(2027, 2, d) for d in (13, 14, 15)], [NONE, VALENTINE, NONE])
        self.assertEqual([self.fest(2026, 6, d) for d in (1, 18, 19, 20)], [CHILDREN, NONE, DRAGON, NONE])
        self.assertEqual(self.fest(2033, 6, 1), DRAGON)        # both on one day: 端午's
        self.assertEqual(self.fest(2036, 6, 1), CHILDREN)

    def test_weather_by_the_season(self) -> None:
        july = self.run_s("weather:7")[0]
        jan = self.run_s("weather:1")[0]
        self.assertEqual(july[SNOW], 0)                        # no snow in summer
        self.assertTrue(350 < july[RAIN] < 650, july)           # about a quarter of days wet
        self.assertTrue(350 < jan[SNOW] < 650, jan)             # about a quarter snowy in winter
        self.assertGreater(july[SUNNY], 900)                   # fair, mostly
        a, b = self.run_s("same:20123:7")[0]
        self.assertEqual(a, b)                                 # the same all day


if __name__ == "__main__":
    unittest.main()

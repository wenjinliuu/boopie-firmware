# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0

"""The pet (pet/boopie_pet.c) through made-up days: hunger, feeding, night,
power-off, daily caps and levels, against docs/boopie-character.md.

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


def hhmm(h: int, m: int) -> int:
    return h * 60 + m


class BoopiePetTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cc = shlex.split(os.environ.get("CC", "cc"))
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "boopie_pet_harness"
            subprocess.run(cc + ["-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(COMPONENT),
                                 "-I", str(COMPONENT / "pet"), str(HERE / "boopie_pet_harness.c"),
                                 str(COMPONENT / "pet" / "boopie_pet.c"), str(COMPONENT / "boopie_expr.c"),
                                 "-o", str(exe)], check=True)
            cls.r = json.loads(subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout)

    def test_hungry_twice_a_day_when_fed(self) -> None:
        # Wakes fed at 8:00; hungry 3.5 h later, fed 10 min on, hungry again
        # 3.5 h after that, then no more that day.
        self.assertEqual(self.r["fed"], [["hungry", hhmm(11, 30)], ["idle", hhmm(11, 41)],
                                         ["hungry", hhmm(15, 10)], ["idle", hhmm(15, 21)]])
        self.assertEqual(self.r["fed_xp"], 10 + 20 + 20)

    def test_ignored_gets_sad_then_sleeps_it_off(self) -> None:
        self.assertEqual(self.r["ignored"], [["hungry", hhmm(11, 30)], ["sad", hhmm(14, 30)], ["idle", hhmm(21, 0)]])
        self.assertEqual(self.r["ignored_xp"], 10)

    def test_late_meals_earn_less(self) -> None:
        self.assertEqual(self.r["late_xp"], 10 + 10 + 10)

    def test_sleepy_at_night_when_left_alone(self) -> None:
        self.assertEqual(self.r["night_alone"], "sleepy")
        self.assertEqual(self.r["night_busy"], "idle")
        self.assertEqual(self.r["early_alone"], "sleepy")
        self.assertEqual(self.r["day_alone"], "idle")

    def test_games_capped_daily(self) -> None:
        # 45 xp and 10 stars a day from games, then room again the next day.
        self.assertEqual(self.r["game"], [45, 10, 15, 3])

    def test_old_saves_load(self) -> None:
        self.assertEqual(self.r["load_v1"], [1, 2, 0, 1])
        self.assertEqual(self.r["load_bad"], [0, 0])

    def test_time_powered_off_doesnt_count(self) -> None:
        self.assertEqual(self.r["off_hungry"], 1)
        self.assertEqual(self.r["off_after"], "hungry")

    def test_waits_for_the_clock(self) -> None:
        self.assertEqual(self.r["unknown"], ["idle", 0])

    def test_daily_caps(self) -> None:
        self.assertEqual(self.r["poke_xp"], 20)
        self.assertEqual(self.r["talk_xp"], 30)
        self.assertEqual(self.r["meet"], [10, 2])

    def test_levels_never_stop(self) -> None:
        self.assertEqual(self.r["levels"], [1, 1, 2, 5, 10, 20, 21])
        self.assertEqual(self.r["need"], [110, 350, 620, 620])

    def test_a_typical_month(self) -> None:
        # About 95 a day: level 11, two stars a day and twenty a level.
        self.assertEqual(self.r["month"], {"xp": 2850, "level": 11, "stars": 30 * 2 + 10 * 20})

    def test_unlock_table(self) -> None:
        # Mint at 2, stars at 3, glitch at 20, party hat at 19, nothing past the list.
        self.assertEqual(self.r["unlocks"], [2, 3, 20, 19, 0])


if __name__ == "__main__":
    unittest.main()

# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0

"""小花园 (pet/boopie_garden.c): plants grow while their soil is damp, through
their stages to a bloom on time if watered daily, only wait if forgotten,
can't be watered while still damp, harvest for their reward, and keep in NVS.

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

SUNFLOWER, TULIP, STRAWBERRY, CACTUS = 1, 2, 3, 4
EMPTY, SEED, SPROUT, LEAVES, BUD, BLOOM = -1, 0, 1, 2, 3, 4


class BoopieGardenTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = Path(cls.tmp.name) / "boopie_garden_harness"
        cc = shlex.split(os.environ.get("CC", "cc"))
        subprocess.run(cc + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(COMPONENT / "pet"),
                             str(HERE / "boopie_garden_harness.c"), str(COMPONENT / "pet" / "boopie_garden.c"),
                             "-o", str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.tmp.cleanup()

    def run_g(self, *steps: str) -> list[dict]:
        r = subprocess.run([str(self.exe), *steps], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        return [json.loads(line) for line in r.stdout.splitlines()]

    def test_watered_daily_it_blooms_on_time_through_its_stages(self) -> None:
        steps = ["plant:0:1"]
        for day in range(1, 3):
            steps += [f"at:{day * 24}", "water:0"]
        s = self.run_g(*steps, "at:71", "at:72")
        stages = [x["pots"][0][0] for x in s]
        self.assertEqual(stages[0], SEED)
        self.assertEqual(s[1]["pots"][0][0], SPROUT)           # a day of three: a third grown
        self.assertEqual(s[3]["pots"][0][0], BUD)
        self.assertEqual(s[-2]["pots"][0][0], BUD)             # an hour short
        self.assertEqual(s[-1]["pots"][0], [BLOOM, 72.0, 0, 0.0])
        self.assertEqual([x["ret"] for x in s[1:5:2]], [0, 0])     # at:H steps
        self.assertTrue(all(x["ret"] for x in s[2:5:2]))           # each day's watering took
        self.assertEqual(stages, sorted(stages))               # never back

    def test_forgotten_it_waits_and_droops_never_dies(self) -> None:
        s = self.run_g("plant:0:2", "at:100", "at:500", "water:0", "at:520")
        self.assertEqual(s[1]["pots"][0][1:3], [30.0, 1])      # grew while damp, then dry
        self.assertEqual(s[2]["pots"][0][1:3], [30.0, 1])      # and waited
        self.assertEqual(s[3]["ret"], 1)
        self.assertEqual(s[4]["pots"][0][1:3], [50.0, 0])      # growing again
        self.assertEqual(s[4]["pots"][0][3], 96 - 50.0)

    def test_no_watering_while_damp_and_nothing_to_water_or_reap(self) -> None:
        s = self.run_g("water:0", "plant:0:1", "water:0", "at:14", "water:0", "at:16", "water:0", "harvest:0",
                       "plant:0:3")
        self.assertEqual([x["ret"] for x in s], [0, 1, 0, 0, 0, 0, 1, 0, 0])
        # 16 h into a 30 h watering, it's a little over half dry: watering takes.

    def test_harvest_rewards_and_empties(self) -> None:
        s = self.run_g("plant:1:4", "at:72", "water:1", "at:144", "water:1", "at:150", "harvest:1", "harvest:1",
                       "plant:1:1")
        self.assertEqual(s[5]["pots"][1][0], BLOOM)
        self.assertEqual((s[6]["ret"], s[6]["xp"], s[6]["stars"]), (1, 30, 5))
        self.assertEqual(s[6]["pots"][1][0], EMPTY)
        self.assertEqual(s[6]["harvested"], [0, 0, 0, 1])
        self.assertEqual(s[7]["ret"], 0)
        self.assertEqual((s[8]["ret"], s[8]["pots"][1][0]), (1, SEED))

    def test_cactus_needs_water_less_often(self) -> None:
        s = self.run_g("plant:2:4", "at:30", "water:2", "at:40", "water:2")
        self.assertEqual(s[2]["ret"], 0)        # 42 h left of 72: still damp
        self.assertEqual(s[4]["ret"], 1)

    def test_three_pots_saved_by_version_1_come_through(self) -> None:
        s = self.run_g("plant:0:1", "plant:2:3", "at:30", "v1", "at:40")
        self.assertEqual(s[3]["ret"], 2)
        self.assertEqual([p[0] for p in s[4]["pots"]], [LEAVES, EMPTY, SPROUT, EMPTY, EMPTY, EMPTY])

    def test_six_plots(self) -> None:
        s = self.run_g("plant:5:4", "plant:6:1")
        self.assertEqual([x["ret"] for x in s], [1, 0])

    def test_keeps_in_nvs(self) -> None:
        s = self.run_g("plant:0:1", "plant:2:3", "at:30", "save")
        self.assertEqual(s[-1]["ret"], 2)       # round trips; a wrong size doesn't load


if __name__ == "__main__":
    unittest.main()

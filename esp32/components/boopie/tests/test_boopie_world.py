# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0

"""The pet's world (world/boopie_world.c): a tap sends the pet to a thing and
uses it once there; the stairs change rooms; the level brings things; it
wanders the floor inside the round screen; the bed puts it to sleep and wakes
it; and every thing that does something can be reached and tapped above the
buttons.

Run from esp32/: python3 -m unittest discover -s components/boopie/tests -p 'test_*.py'
"""

from __future__ import annotations

import json
import math
import os
import shlex
import subprocess
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
COMPONENT = HERE.parent

(NOTHING, GAMES, BOOKS, RADIO, FEED, UPSTAIRS, DOWNSTAIRS, OUTSIDE, SLEEP, WARDROBE, RENAME, STATUS,
 PLOT, INSIDE, MAIL, WILD) = range(16)
IDLE, WALKING, USING, SLEEPING = range(4)
LIVING, BEDROOM, YARD = range(3)


class BoopieWorldTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = Path(cls.tmp.name) / "boopie_world_harness"
        cc = shlex.split(os.environ.get("CC", "cc"))
        subprocess.run(cc + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(COMPONENT / "world"),
                             str(HERE / "boopie_world_harness.c"), str(COMPONENT / "world" / "boopie_world.c"),
                             str(COMPONENT / "world" / "boopie_world_art.c"), "-lm", "-o", str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.tmp.cleanup()

    def run_w(self, *steps: str):
        r = subprocess.run([str(self.exe), *steps], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        return [json.loads(line) for line in r.stdout.splitlines()]

    def things(self, level: int) -> list[dict]:
        return self.run_w(f"lv:{level}", "things")[1]

    def thing(self, level: int, act: int, room: int = LIVING) -> dict:
        return next(t for t in self.things(level) if t["act"] == act and t["room"] == room and t["shown"])

    def test_a_tap_sends_it_to_use_a_thing_once(self) -> None:
        tv = self.thing(1, GAMES)
        cx, cy = (tv["box"][0] + tv["box"][2]) / 2, tv["box"][3] - 6
        s = self.run_w(f"tap:{cx}:{cy}", "tick:0.2", "tick:6", "tick:1")
        self.assertEqual(s[0]["act"], GAMES)
        self.assertEqual(s[1]["state"], WALKING)
        self.assertEqual(s[2]["acts"], [GAMES])          # used once, on arriving
        self.assertAlmostEqual(s[2]["x"], tv["use"][0], delta=1.5)
        self.assertEqual(s[3]["acts"], [])

    def test_the_stairs_go_up_and_down(self) -> None:
        up = self.thing(1, UPSTAIRS)
        down = self.thing(1, DOWNSTAIRS, BEDROOM)
        s = self.run_w(f"tap:{up['x']}:{up['y'] - 8}", "tick:3",
                       f"tap:{down['x']}:{down['y'] - 6}", "tick:3")
        self.assertEqual((s[1]["room"], s[1]["acts"]), (BEDROOM, [UPSTAIRS]))
        self.assertLess(math.hypot(s[1]["x"] - down["use"][0], s[1]["y"] - down["use"][1]), 12)
        self.assertEqual((s[3]["room"], s[3]["acts"]), (LIVING, [DOWNSTAIRS]))

    def test_the_level_brings_things(self) -> None:
        def shown(level: int) -> set[int]:
            return {t["art"] for t in self.things(level) if t["shown"]}
        lv1, lv10, lv20 = shown(1), shown(10), shown(20)
        self.assertLess(len(lv1), len(lv10))
        self.assertLess(len(lv10), len(lv20))
        # Something does each job at every level: the old TV gives way to the flat one.
        for level in (1, 14, 15, 30):
            acts = {t["act"] for t in self.things(level) if t["shown"]}
            self.assertTrue({GAMES, BOOKS, RADIO, FEED, UPSTAIRS, DOWNSTAIRS, OUTSIDE, SLEEP, WARDROBE, RENAME,
                             STATUS, PLOT, INSIDE, MAIL, WILD} <= acts, level)
            games = [t for t in self.things(level) if t["shown"] and t["act"] == GAMES]
            self.assertEqual(len(games), 1)

    def test_everything_tappable_is_on_the_round_screen_above_the_buttons(self) -> None:
        for level in (1, 20):
            for t in self.things(level):
                if not t["shown"]:
                    continue
                if t["room"] == YARD:
                    # The view follows the pet across: only up and down must fit.
                    if t["act"]:
                        hint_over = 0 if t["act"] == INSIDE else 16   # the house's hint is over its door
                        self.assertGreaterEqual(t["box"][1] + 16 - hint_over, 6, t)
                        self.assertLessEqual(t["box"][3], 128, t)
                        self.assertLess(0, t["box"][0], t)
                        self.assertLess(t["box"][2], 360, t)
                    continue
                x0, y0, x1, y1 = t["box"]
                top = y0 + (16 if t["act"] and t["art"] else 0)   # the picture, under its hint
                for x, y in ((x0 + 2, top + 2), (x1 - 2, top + 2), ((x0 + x1) / 2, y1), ((x0 + x1) / 2, y0)):
                    self.assertLess(math.hypot(x - 78, y - 78), 78, t)
                if t["act"]:
                    self.assertLessEqual(y1, 128, t)     # the buttons start ~130 down
                    ux, uy = t["use"]
                    self.assertLess(math.hypot(ux - 78, uy - 78), 70, t)

    def test_it_wanders_the_floor_inside_the_circle(self) -> None:
        s = self.run_w("wander:600")[0]
        self.assertLessEqual(s["far"], 66.5)
        self.assertGreater(s["walked"], 300)               # it does wander
        self.assertGreaterEqual(s["y"][0], 70)
        self.assertLessEqual(s["y"][1], 124)

    def test_the_bed_puts_it_to_sleep_and_wakes_it(self) -> None:
        bed = self.thing(1, SLEEP, BEDROOM)
        up = self.thing(1, UPSTAIRS)
        s = self.run_w(f"tap:{up['x']}:{up['y'] - 8}", "tick:8", f"tap:{bed['x']}:{bed['y'] - 10}", "tick:6",
                       "tick:30", "tap:100:100", "tick:0.1",
                       "sleep:1", "tick:6", f"tap:{bed['x']}:{bed['y'] - 10}", "tick:1")
        self.assertEqual(s[3]["acts"], [SLEEP])
        self.assertEqual(s[4]["state"], SLEEPING)          # and stays asleep
        self.assertEqual(s[5]["tap"], -3)                  # a tap elsewhere wakes it
        self.assertNotEqual(s[6]["state"], SLEEPING)
        self.assertEqual(s[8]["state"], SLEEPING)          # sent to bed
        self.assertEqual(s[10]["acts"], [SLEEP])           # the bed again: up

    def test_out_the_door_and_back(self) -> None:
        mat = self.thing(1, OUTSIDE)
        door = self.thing(1, INSIDE, YARD)
        s = self.run_w(f"tap:{mat['x']}:{mat['y'] - 3}", "tick:3", f"tap:{door['x']}:{door['y'] - 10}", "tick:3")
        self.assertEqual((s[1]["room"], s[1]["acts"]), (YARD, [OUTSIDE]))
        self.assertLess(math.hypot(s[1]["x"] - door["use"][0], s[1]["y"] - door["use"][1]), 3)
        self.assertEqual((s[3]["room"], s[3]["acts"]), (LIVING, [INSIDE]))

    def test_the_farm_opens_with_the_level(self) -> None:
        def plots(level: int) -> int:
            return sum(1 for t in self.things(level) if t["act"] == PLOT and t["shown"])
        self.assertEqual([plots(lv) for lv in (1, 5, 10, 15)], [3, 4, 5, 6])

    def test_floor_and_wall(self) -> None:
        s = self.run_w("tap:90:90", "tick:1.5", "tap:78:10")
        self.assertEqual(s[0]["tap"], -1)
        self.assertAlmostEqual(s[1]["x"], 90, delta=1)
        self.assertEqual(s[2]["tap"], -2)                  # the wall: nothing


if __name__ == "__main__":
    unittest.main()

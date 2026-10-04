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
 PLOT, INSIDE, MAIL, WILD, HOME_PATH, CHEST, SLIME_FIGHT, SLIME_WIN, SLIME_FLED, GATHER, FURNI,
 BEACH_SIGN, FISH, FISH_BITE, FISH_CAUGHT, FISH_EARLY, FISH_MISSED, ANTIC, DECOR) = range(31)
IDLE, WALKING, USING, SLEEPING, AT_IT = range(5)
NONE, TV, DANCE, MIRROR, LOVE, SWING, BUTTERFLY, SWIM = range(8)
LIVING, BEDROOM, YARD, WOODS, BEACH = range(5)
WIDTH = {YARD: 360, WOODS: 520, BEACH: 440}
GREEN, BLUE, PINK, GOLD = range(4)
AWAY, ROAM, FIGHTING, POOF = range(4)


class BoopieWorldTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = Path(cls.tmp.name) / "boopie_world_harness"
        cc = shlex.split(os.environ.get("CC", "cc"))
        subprocess.run(cc + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(COMPONENT / "world"),
                             str(HERE / "boopie_world_harness.c"), str(COMPONENT / "world" / "boopie_world.c"),
                             str(COMPONENT / "world" / "boopie_world_art.c"),
                             str(COMPONENT / "world" / "boopie_season.c"), "-lm", "-o", str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.tmp.cleanup()

    def run_w(self, *steps: str):
        r = subprocess.run([str(self.exe), *steps], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        return [json.loads(line) for line in r.stdout.splitlines()]

    def things(self, level: int, furniture: int = 63) -> list[dict]:
        return self.run_w(f"furni:{furniture}", f"lv:{level}", "things")[2]

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
                if t["room"] in WIDTH:
                    # The view follows the pet across: only up and down must fit.
                    if t["act"]:
                        # the house's hint is over its door, the pier's halfway out
                        hint_over = 0 if t["act"] in (INSIDE, FISH) else 16
                        self.assertGreaterEqual(t["box"][1] + 16 - hint_over, 6, t)
                        self.assertLessEqual(t["box"][3], 128, t)
                        self.assertLess(0, t["box"][0], t)
                        self.assertLess(t["box"][2], WIDTH[t["room"]], t)
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

    def test_to_the_woods_and_back(self) -> None:
        sign = self.thing(1, WILD, YARD)
        home = self.thing(1, HOME_PATH, WOODS)
        mat = self.thing(1, OUTSIDE)
        s = self.run_w(f"tap:{mat['x']}:{mat['y'] - 3}", "tick:3", f"tap:{sign['x']}:{sign['y'] - 8}", "tick:10.5",
                       f"tap:{home['x']}:{home['y'] - 8}", "tick:1.5")
        self.assertEqual((s[3]["room"], s[3]["acts"]), (WOODS, [WILD]))
        self.assertLess(math.hypot(s[3]["x"] - home["use"][0], s[3]["y"] - home["use"][1]), 3)   # in by its sign
        self.assertEqual((s[5]["room"], s[5]["acts"]), (YARD, [HOME_PATH]))
        self.assertLess(math.hypot(s[5]["x"] - sign["use"][0], s[5]["y"] - sign["use"][1]), 3)

    def test_the_woods_chests_come_with_the_level(self) -> None:
        def chests(level: int) -> int:
            return sum(1 for t in self.things(level) if t["act"] == CHEST and t["shown"])
        self.assertEqual([chests(lv) for lv in (1, 7, 8)], [1, 1, 2])

    def test_a_slime_is_chased_fought_and_beaten(self) -> None:
        s = self.run_w("woods", "chase", "hit:6", "tick:1", "slimes", "tick:41", "slimes")
        chase, hit = s[1], s[2]
        self.assertEqual((chase["tap"], chase["fight"]), (-4, 1))
        self.assertLess(chase["secs"], 10)
        hp = chase["hp"]
        self.assertEqual(hit["taps"][:hp], [-5] * hp)          # every tap on it a hit ...
        self.assertEqual(hit["acts"], [SLIME_WIN])             # ... till it's beaten
        self.assertEqual((hit["fight"], hit["last"]), (-1, chase["kind"]))
        self.assertLess(hit["moved"], 1)                       # the pet stands its ground
        self.assertEqual(s[4][chase["slime"]]["state"], AWAY)
        self.assertEqual(s[6][chase["slime"]]["state"], ROAM)  # back a while later

    def test_it_stays_in_view_and_above_the_buttons_and_can_get_away(self) -> None:
        s = self.run_w("woods", "chase", "watch:11", "watch:1.5")
        self.assertEqual(s[1]["fight"], 1)
        lo, hi = s[2]["view"]
        self.assertGreaterEqual(lo, 12)                        # round the pet, on screen ...
        self.assertLessEqual(hi, 144)
        self.assertLessEqual(s[2]["y"], 114.5)                 # ... and above the buttons
        self.assertEqual((s[2]["fled"], s[3]["fled"]), (0, 1))  # 12 s, then it's off
        s = self.run_w("woods", "chase", "tap:5:60", "tick:0.1")
        self.assertEqual(s[2]["tap"], -6)                      # a miss, and nothing else
        self.assertEqual(s[3]["state"], IDLE)

    def test_slimes_by_level(self) -> None:
        low = self.run_w("lv:1", "kinds:300")[1]
        self.assertEqual((low[BLUE], low[PINK]), (0, 0))
        self.assertGreater(low[GREEN], 200)
        self.assertGreater(low[GOLD], 0)                       # now and then a gold one
        high = self.run_w("lv:12", "kinds:300")[1]
        self.assertTrue(all(high[k] > 0 for k in range(4)), high)

    def test_furniture_shows_once_its_out(self) -> None:
        def out(mask: int) -> list[int]:
            return sorted(t["art"] for t in self.things(1, mask) if t["act"] == FURNI and t["shown"])
        self.assertEqual(out(0), [])
        self.assertEqual(len(out(63)), 6)
        self.assertEqual(len(out(1 | 4)), 2)

    def test_the_woods_have_things_to_pick(self) -> None:
        def spots(level: int) -> list[int]:
            return sorted(t["x"] for t in self.things(level)
                          if t["act"] == GATHER and t["shown"] and t["room"] == WOODS)
        self.assertEqual(len(spots(1)), 3)                     # mushrooms ...
        self.assertEqual(len(spots(4)), 5)                     # ... and berry bushes from 4

    def test_to_the_beach_and_back(self) -> None:
        sign = self.thing(1, BEACH_SIGN, YARD)
        home = self.thing(1, HOME_PATH, BEACH)
        mat = self.thing(1, OUTSIDE)
        s = self.run_w(f"tap:{mat['x']}:{mat['y'] - 3}", "tick:3", f"tap:{sign['x']}:{sign['y'] - 8}", "tick:4",
                       f"tap:{home['x']}:{home['y'] - 8}", "tick:1.5")
        self.assertEqual((s[3]["room"], s[3]["acts"]), (BEACH, [BEACH_SIGN]))
        self.assertLess(math.hypot(s[3]["x"] - home["use"][0], s[3]["y"] - home["use"][1]), 3)
        self.assertEqual((s[5]["room"], s[5]["acts"]), (YARD, [HOME_PATH]))
        self.assertLess(math.hypot(s[5]["x"] - sign["use"][0], s[5]["y"] - sign["use"][1]), 3)   # back by its sign
        shells = [t for t in self.things(1) if t["room"] == BEACH and t["act"] == GATHER and t["shown"]]
        self.assertEqual(len(shells), 3)

    def fish(self, *after: str):
        pier = self.thing(1, FISH, BEACH)
        return self.run_w("beach", f"tap:{pier['x']}:{pier['y'] - 30}", "tick:16", *after), pier

    def test_out_along_the_pier_to_fish(self) -> None:
        s, pier = self.fish()
        self.assertEqual(s[1]["tap"] >= 0, True)
        self.assertIn(FISH, s[2]["acts"])
        self.assertAlmostEqual(s[2]["x"], pier["use"][0], delta=1)   # out at its end, over the water
        self.assertAlmostEqual(s[2]["y"], pier["use"][1], delta=1)

    def test_a_bite_then_a_tap_lands_it(self) -> None:
        s, _ = self.fish("tick:7", "tap:10:120", "tick:0.1")
        self.assertIn(FISH_BITE, s[2]["acts"] + s[3]["acts"])
        # Ticks of 7 s run past a 1.2 s bite: it got away ...
        self.assertIn(FISH_MISSED, s[2]["acts"] + s[3]["acts"])
        s, _ = self.fish("tap:10:120", "tick:0.1")
        self.assertEqual(s[3]["tap"], -8)                      # ... too soon: scared off
        self.assertEqual(s[4]["acts"], [FISH_EARLY])

    def test_a_tap_in_the_bite_catches(self) -> None:
        caught = 0
        for wait in (2.0, 2.5, 3.0, 3.5, 4.0, 4.5, 5.0, 5.5, 6.0):
            s, _ = self.fish(f"tick:{wait}", "tap:10:120", "tick:0.1")
            if s[4]["tap"] == -7:
                self.assertEqual(s[5]["acts"], [FISH_CAUGHT])
                caught += 1
        self.assertGreater(caught, 0)

    def test_each_room_has_its_little_somethings(self) -> None:
        def can(room: int, level: int, furniture: int = 0) -> list[int]:
            return [a for a in range(1, 8)
                    if self.run_w(f"furni:{furniture}", f"lv:{level}", f"room:{room}", f"antic:{a}")[3]["ok"]]
        self.assertEqual(can(LIVING, 1), [TV])
        self.assertEqual(can(LIVING, 1, 63), [TV, DANCE])                 # the record player bought
        self.assertEqual(can(BEDROOM, 1), [MIRROR])
        self.assertEqual(can(BEDROOM, 1, 63), [MIRROR, LOVE])             # the teddy
        self.assertEqual(can(YARD, 1), [BUTTERFLY])
        self.assertEqual(can(YARD, 5, 63), [LOVE, SWING, BUTTERFLY])      # the flowers, the swing
        self.assertEqual(can(WOODS, 1), [BUTTERFLY])
        self.assertEqual(can(BEACH, 1), [SWIM])
        self.assertEqual(can(BEACH, 7), [LOVE, SWIM])                     # its sandcastle

    def test_a_swim_goes_in_and_comes_back_out(self) -> None:
        s = self.run_w("room:4", "antic:7", "tick:6", "tick:12", "tick:6")
        self.assertIn(ANTIC, s[2]["acts"])
        self.assertLess(s[2]["min_y"], 45)                     # out in the water
        self.assertEqual(s[4]["antic"], NONE)
        self.assertGreaterEqual(s[4]["y"], 66)                 # back on the sand

    def test_a_butterfly_is_chased_and_a_tap_ends_it(self) -> None:
        s = self.run_w("room:2", "antic:6", "tick:4", "tap:100:110", "tick:0.1")
        self.assertEqual(s[1]["ok"], 1)
        self.assertEqual((s[2]["state"], s[2]["antic"]), (AT_IT, BUTTERFLY))
        self.assertEqual(s[4]["antic"], NONE)
        self.assertEqual(s[4]["state"], WALKING)               # off where it was tapped

    def test_left_alone_it_gets_up_to_things(self) -> None:
        s = self.run_w("furni:63", "lv:10", "tick:240")
        self.assertIn(ANTIC, s[2]["acts"])

    def test_festivals_and_the_weather_put_things_out(self) -> None:
        def decor(fest: int, weather: int) -> int:
            s = self.run_w(f"season:{fest}:{weather}", "lv:1", "things")
            return sum(1 for t in s[2] if t["act"] == DECOR and t["shown"])
        self.assertEqual(decor(0, 0), 0)
        self.assertEqual(decor(1, 0), 3)                       # 春节: two lanterns and the couplets
        self.assertEqual(decor(2, 0), 3)                       # 中秋: lanterns, mooncakes
        self.assertEqual(decor(4, 3), 3)                       # 圣诞 in the snow: wreath, tree, snowman
        self.assertEqual(decor(0, 2), 2)                       # rain: puddles

    def test_no_butterflies_or_swims_in_the_rain(self) -> None:
        self.assertEqual(self.run_w("season:0:2", "room:2", "antic:6")[2]["ok"], 0)
        self.assertEqual(self.run_w("season:0:3", "room:4", "antic:7")[2]["ok"], 0)
        self.assertEqual(self.run_w("season:0:1", "room:4", "antic:7")[2]["ok"], 1)   # cloudy's fine

    def test_floor_and_wall(self) -> None:
        s = self.run_w("tap:90:90", "tick:1.5", "tap:78:10")
        self.assertEqual(s[0]["tap"], -1)
        self.assertAlmostEqual(s[1]["x"], 90, delta=1)
        self.assertEqual(s[2]["tap"], -2)                  # the wall: nothing


if __name__ == "__main__":
    unittest.main()

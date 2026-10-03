# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0

"""接零食 and 重力迷宫 (game/boopie_catch.c, game/boopie_maze.c), played by bots:
a round ends on time, playing well scores and earns the reward, standing
still doesn't; every maze is perfect and the ball never goes through a wall.

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


class BoopieGamesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = Path(cls.tmp.name) / "boopie_games_harness"
        cc = shlex.split(os.environ.get("CC", "cc"))
        subprocess.run(cc + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(COMPONENT / "game"),
                             str(HERE / "boopie_games_harness.c"), str(COMPONENT / "game" / "boopie_catch.c"),
                             str(COMPONENT / "game" / "boopie_maze.c"), "-lm", "-o", str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.tmp.cleanup()

    def play(self, game: str, seed: int, bot: str) -> dict:
        r = subprocess.run([str(self.exe), game, str(seed), bot], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        return json.loads(r.stdout)

    def test_catch_chasing_scores_and_earns(self) -> None:
        for seed in (1, 7, 42, 2026):
            g = self.play("catch", seed, "chase")
            self.assertAlmostEqual(g["t"], 60.0, places=1)
            self.assertGreater(g["caught"], 40, g)
            self.assertGreater(g["caught"], 3 * g["missed"], g)
            self.assertGreater(g["golds"], 0, g)
            self.assertGreaterEqual(g["best_combo"], 5, g)
            self.assertGreaterEqual(g["xp"], 10, g)
            self.assertGreaterEqual(g["stars"], 1, g)
            # The pet stays inside the round screen.
            self.assertGreaterEqual(g["min_x"], 13.0)
            self.assertLessEqual(g["max_x"], 51.0)

    def test_catch_standing_still_earns_little(self) -> None:
        for seed in (1, 7, 42):
            g = self.play("catch", seed, "still")
            self.assertLess(g["score"], 25, g)
            self.assertEqual(g["xp"], 0)
            self.assertEqual((g["min_x"], g["max_x"]), (32.0, 32.0))

    def test_maze_solving_clears_bigger_mazes(self) -> None:
        for seed in (1, 7, 42, 2026, 99):
            g = self.play("maze", seed, "solve")
            self.assertEqual(g["bad"], 0, g)
            self.assertEqual(g["in_wall"], 0, g)
            self.assertGreaterEqual(g["level"], 4, g)
            self.assertEqual(g["sizes"][:4], [4, 5, 6, 7], g)
            self.assertLessEqual(g["max_speed"], 34.01)
            self.assertGreater(g["xp"], 0, g)

    def test_maze_level_board_goes_nowhere(self) -> None:
        g = self.play("maze", 3, "still")
        self.assertEqual((g["score"], g["level"], g["in_wall"]), (0, 0, 0))
        self.assertEqual((g["x"], g["y"]), (3.5, 3.5))


if __name__ == "__main__":
    unittest.main()

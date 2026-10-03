# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0

"""戳戳布比's rules (game/boopie_whack.c), played by a few kinds of player."""

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


class BoopieWhackTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "boopie_whack_harness"
            cc = shlex.split(os.environ.get("CC", "cc"))
            subprocess.run(cc + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(COMPONENT / "game"),
                                 str(HERE / "boopie_whack_harness.c"), str(COMPONENT / "game" / "boopie_whack.c"),
                                 "-lm", "-o", str(exe)], check=True)
            cls.r = json.loads(subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout)

    def test_a_round_is_a_minute(self) -> None:
        for name in ("perfect", "slow", "idle"):
            self.assertAlmostEqual(self.r[name]["seconds"], 60.0, delta=0.1)

    def test_same_seed_same_round(self) -> None:
        self.assertEqual(self.r["perfect"], self.r["perfect_again"])

    def test_a_good_player_earns_the_top_reward(self) -> None:
        p = self.r["perfect"]
        self.assertEqual(p["misses"], 0)
        self.assertGreaterEqual(p["score"], 120, p)
        self.assertEqual([p["xp"], p["stars"]], [15, 3])
        self.assertGreaterEqual(p["best_combo"], 5)

    def test_slow_players_miss_more_later(self) -> None:
        s = self.r["slow"]
        self.assertGreater(s["misses"], 0)
        self.assertLess(s["score"], self.r["perfect"]["score"])

    def test_clouds_cost_points(self) -> None:
        c = self.r["careless"]
        self.assertGreater(c["clouds_hit"], 0)
        self.assertLess(c["score"], self.r["perfect"]["score"])

    def test_it_gets_busier(self) -> None:
        self.assertEqual(self.r["slow"]["max_up"], 3)   # quick players clear them too fast to see it

    def test_doing_nothing_scores_nothing(self) -> None:
        i = self.r["idle"]
        self.assertEqual([i["score"], i["xp"], i["stars"]], [0, 0, 0])
        self.assertGreater(i["misses"], 20)

    def test_taps_on_nothing_score_nothing(self) -> None:
        self.assertEqual(self.r["empty_tap"], [0, 0])

    def test_rewards(self) -> None:
        self.assertEqual(self.r["rewards"], [[0, 0], [5, 0], [10, 1], [15, 2], [15, 2], [15, 3]])


if __name__ == "__main__":
    unittest.main()

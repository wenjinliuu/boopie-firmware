# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0

"""How the board is held (pet/boopie_posture.c): face down, turned over,
upside down and back, picked up after lying still; and ordinary handling,
fast or slow readings, setting none of them off.

Run from esp32/: python3 -m unittest discover -s components/boopie/tests -p 'test_*.py'
"""

from __future__ import annotations

import os
import shlex
import subprocess
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
COMPONENT = HERE.parent

NONE, FACE_DOWN, FACE_UP, UPSIDE_DOWN, RIGHT_WAY_UP, LIFTED = range(6)

FLAT = (0, 0, -1)        # lying face up: gravity into the screen
UPRIGHT = (0, 0.95, -0.3)  # held up to read
TILTED = (0.15, 0.55, -0.82)
DOWN = (0, 0, 1)
UPSIDE = (0, -0.95, -0.3)


def at(g, dt: float, n: int) -> str:
    return f"{g[0]},{g[1]},{g[2]},{dt},{n}"


class BoopiePostureTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = Path(cls.tmp.name) / "boopie_posture_harness"
        cc = shlex.split(os.environ.get("CC", "cc"))
        subprocess.run(cc + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(COMPONENT / "pet"),
                             str(HERE / "boopie_posture_harness.c"), str(COMPONENT / "pet" / "boopie_posture.c"),
                             "-lm", "-o", str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.tmp.cleanup()

    def events(self, *steps: str) -> list[tuple[int, int]]:
        r = subprocess.run([str(self.exe), *steps], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        return [tuple(map(int, line.split())) for line in r.stdout.splitlines()]

    def test_picked_up_after_lying_still(self) -> None:
        # Once a second, as with the screen asleep: 12 s on the table, then lifted.
        self.assertEqual(self.events(at(FLAT, 1, 12), at(TILTED, 1, 3)), [(12, LIFTED)])
        # Not before it has settled.
        self.assertEqual(self.events(at(FLAT, 1, 5), at(TILTED, 1, 3)), [])
        # Nudged a little on the table: no.
        self.assertEqual(self.events(at(FLAT, 1, 12), at((0.1, 0.1, -0.99), 1, 3)), [])
        # Fast readings, as with the screen on.
        self.assertEqual(self.events(at(FLAT, 0.05, 220), at(UPRIGHT, 0.05, 10)), [(220, LIFTED)])

    def test_face_down_and_back(self) -> None:
        ev = self.events(at(FLAT, 0.05, 40), at(DOWN, 0.05, 30), at(FLAT, 0.05, 20))
        self.assertEqual([e for _, e in ev], [FACE_DOWN, FACE_UP])
        self.assertEqual(ev[0][0], 40 + 19)   # after a second face down
        self.assertEqual(ev[1][0], 70 + 5)    # after 0.3 s the right way up
        # At once a second too, and turning it over is no lift as well.
        ev = self.events(at(FLAT, 1, 15), at(DOWN, 1, 3), at(FLAT, 1, 15), at(TILTED, 1, 2))
        self.assertEqual([e for _, e in ev], [FACE_DOWN, FACE_UP, LIFTED])
        # A glance at its back doesn't count.
        self.assertEqual(self.events(at(FLAT, 0.05, 40), at(DOWN, 0.05, 10), at(FLAT, 0.05, 20)), [])

    def test_upside_down_and_back(self) -> None:
        ev = self.events(at(UPRIGHT, 0.05, 20), at(UPSIDE, 0.05, 30), at(UPRIGHT, 0.05, 10))
        self.assertEqual([e for _, e in ev], [UPSIDE_DOWN, RIGHT_WAY_UP])
        self.assertEqual(ev[0][0], 20 + 11)
        # Turned past it quickly: nothing.
        self.assertEqual(self.events(at(UPRIGHT, 0.05, 20), at(UPSIDE, 0.05, 5), at(UPRIGHT, 0.05, 10)), [])

    def test_ordinary_handling_sets_nothing_off(self) -> None:
        steps = []
        for i in range(60):
            g = (0.3 * ((i % 7) - 3) / 3, 0.7 + 0.2 * ((i % 5) - 2) / 2, -0.5 - 0.1 * (i % 3))
            steps.append(at(g, 0.05, 4))
        self.assertEqual(self.events(at(UPRIGHT, 0.05, 20), *steps), [])


if __name__ == "__main__":
    unittest.main()

# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0

"""The shake detector (device/boopie_shake.c) on made-up accelerometer traces.

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


class BoopieShakeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cc = shlex.split(os.environ.get("CC", "cc"))
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "boopie_shake_harness"
            subprocess.run(cc + ["-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(COMPONENT / "device"),
                                 str(HERE / "boopie_shake_harness.c"), str(COMPONENT / "device" / "boopie_shake.c"),
                                 "-lm", "-o", str(exe)], check=True)
            cls.counts = json.loads(subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout)

    def test_calm_handling_is_not_a_shake(self) -> None:
        for trace in ("still", "walking", "knock", "set_down", "tilt"):
            self.assertEqual(self.counts[trace], 0, trace)

    def test_a_shake_counts_once(self) -> None:
        self.assertEqual(self.counts["shake_short"], 1)

    def test_long_shaking_waits_between(self) -> None:
        # Ten seconds of shaking, a cooldown of four after each.
        self.assertIn(self.counts["shake_long"], (2, 3))


if __name__ == "__main__":
    unittest.main()

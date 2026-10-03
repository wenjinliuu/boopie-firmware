# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0

"""The C avatar renderer (avatar/boopie_pixel.c) against its reference, the
prototype in tools/boopie/avatar_proto.py: every character, expression and
overlay, a few frames each, rendered by both and compared pixel by pixel.

The two round alike (half to even) but the C does its per-pixel work in float
where Python has double, so a pixel landing right on a boundary can go the
other way: a few per frame are allowed, no more.

Run from esp32/: python3 -m unittest discover -s components/boopie/tests -p 'test_*.py'
Needs NumPy and Pillow (the prototype's); skipped without them.
"""

from __future__ import annotations

import os
import shlex
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
COMPONENT = HERE.parent
REPO = COMPONENT.parent.parent.parent

try:
    import numpy as np
    sys.path.insert(0, str(REPO / "tools" / "boopie"))
    import avatar_proto as ap
except ImportError:          # pragma: no cover - depends on the machine
    ap = None

N = 64
MAX_FRAME_DIFF = 0.015       # of a frame's pixels
MAX_MEAN_DIFF = 0.002
STEP = 2                     # every other frame of the prototype's 12 fps loops
UNMATCHED = {"confetti"}     # random in Python, hashed in C


@unittest.skipIf(ap is None, "needs NumPy and Pillow")
class BoopiePixelTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = Path(cls.tmp.name) / "boopie_pixel_harness"
        cc = shlex.split(os.environ.get("CC", "cc"))
        subprocess.run(
            cc + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                  "-I", str(COMPONENT), "-I", str(COMPONENT / "avatar"),
                  str(HERE / "boopie_pixel_harness.c"), str(COMPONENT / "avatar" / "boopie_pixel.c"),
                  str(COMPONENT / "boopie_expr.c"), "-lm", "-o", str(cls.exe)],
            check=True)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.tmp.cleanup()

    def render_c(self, lines: list[str]) -> "np.ndarray":
        out = subprocess.run([str(self.exe)], input="".join(lines).encode(), capture_output=True,
                             check=True).stdout
        return np.frombuffer(out, np.uint8).reshape(-1, N, N, 3)

    def compare(self, jobs) -> None:
        """jobs: (rig class, expression, t, overlay or None) each."""
        lines = [f"{R.key} {e} {t!r} {o} {t!r}\n" if o else f"{R.key} {e} {t!r}\n" for R, e, t, o in jobs]
        frames = self.render_c(lines)
        loops = {n: ln for n, ln, _ in ap.EXPRESSIONS}
        overlay_loops = {n: ln for n, ln, _ in ap.OVERLAYS}
        diffs = []
        for (R, e, t, o), got in zip(jobs, frames):
            p = ap.pose_for(e, t, loops[e])
            if o:
                p = ap.overlay(p, o, t, overlay_loops[o])
            want = ap.render(R(), p)
            diff = float((want != got).any(-1).mean())
            diffs.append(diff)
            self.assertLessEqual(diff, MAX_FRAME_DIFF, f"{R.key} {o or e} at {t:.3f} s: {diff:.2%} differ")
        self.assertLessEqual(sum(diffs) / len(diffs), MAX_MEAN_DIFF)

    def test_expressions(self) -> None:
        self.assertEqual([n for n, _, _ in ap.EXPRESSIONS],
                         ["boot", "idle", "listening", "thinking", "speaking", "error", "off", "happy",
                          "hungry", "eating", "sleepy", "sad", "dizzy"])
        jobs = [(R, name, i / ap.FPS, None)
                for R in ap.CHARACTERS for name, length, _ in ap.EXPRESSIONS
                for i in range(0, round(length * ap.FPS), STEP)]
        self.compare(jobs)

    def test_overlays(self) -> None:
        self.assertEqual([n for n, _, _ in ap.OVERLAYS],
                         ["surprise", "blush", "confetti", "hearts", "low_battery", "charging"])
        jobs = [(R, "idle", i / ap.FPS, name)
                for R in ap.CHARACTERS for name, length, _ in ap.OVERLAYS if name not in UNMATCHED
                for i in range(0, round(length * ap.FPS), STEP)]
        self.compare(jobs)

    def test_characters(self) -> None:
        self.assertEqual([R.key for R in ap.CHARACTERS], ["boopie", "gpt", "codex", "klaude", "whale", "doubao"])


if __name__ == "__main__":
    unittest.main()

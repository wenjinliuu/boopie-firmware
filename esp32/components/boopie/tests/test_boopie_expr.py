# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0

"""The expression table (boopie_expr.c) against the spec in docs/boopie-character.md.

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
ESP32 = COMPONENT.parent.parent

CORE = ["boot", "idle", "listening", "thinking", "speaking", "error", "off", "happy"]

# Pet expression -> fallback, from docs/boopie-character.md "三、表情".
EXTENDED = {
    "hungry": "idle",
    "eating": "happy",
    "sleepy": "off",
    "sad": "idle",
    "dizzy": "error",
}

OVERLAYS = ["surprise", "blush", "confetti", "hearts", "low_battery", "charging", "food"]


def run_harness() -> dict:
    cc = shlex.split(os.environ.get("CC", "cc"))
    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp) / "boopie_expr_harness"
        subprocess.run(
            cc + ["-std=c11", "-Wall", "-Wextra", "-Werror",
                  "-I", str(COMPONENT), "-I", str(ESP32 / "components" / "muse"),
                  str(HERE / "boopie_expr_harness.c"), str(COMPONENT / "boopie_expr.c"),
                  "-o", str(exe)],
            check=True)
        out = subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout
    return json.loads(out)


class BoopieExprTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.data = run_harness()
        cls.by_name = {e["name"]: e for e in cls.data["exprs"]}

    def test_ids_in_spec_order(self) -> None:
        names = [e["name"] for e in self.data["exprs"]]
        self.assertEqual(names, CORE + list(EXTENDED))
        self.assertEqual(self.data["core_count"], len(CORE))
        self.assertEqual([e["id"] for e in self.data["exprs"]], list(range(self.data["count"])))

    def test_names_round_trip(self) -> None:
        for e in self.data["exprs"]:
            self.assertTrue(e["round_trip"], e["name"])

    def test_core_falls_back_to_itself(self) -> None:
        for name in CORE:
            e = self.by_name[name]
            self.assertTrue(e["core"], name)
            self.assertEqual(e["fallback"], name)
            self.assertEqual(e["core_only"], name)

    def test_extended_fallbacks_match_spec(self) -> None:
        for name, fallback in EXTENDED.items():
            e = self.by_name[name]
            self.assertFalse(e["core"], name)
            self.assertEqual(e["fallback"], fallback, name)

    def test_every_chain_ends_at_core(self) -> None:
        for name in EXTENDED:
            seen = [name]
            while name not in CORE:
                name = self.by_name[name]["fallback"]
                self.assertNotIn(name, seen, f"cycle: {seen}")
                seen.append(name)
            self.assertEqual(self.by_name[seen[0]]["core_only"], name)

    def test_resolve(self) -> None:
        for e in self.data["exprs"]:
            self.assertEqual(e["all"], e["name"])
        self.assertEqual(self.by_name["sleepy"]["core_only"], "off")
        self.assertEqual(self.data["partial"], {"eating": "happy", "sleepy": "sleepy"})
        self.assertEqual(self.data["thinking_empty_set"], "thinking")

    def test_bad_input(self) -> None:
        self.assertFalse(self.data["unknown_found"])
        self.assertFalse(self.data["null_found"])
        self.assertTrue(self.data["unknown_untouched"])
        self.assertEqual(self.data["out_of_range"], {
            "valid_neg": False, "valid_count": False, "name": None,
            "resolve": "idle", "fallback": "idle",
        })

    def test_overlays(self) -> None:
        self.assertEqual(self.data["overlay_count"], len(OVERLAYS))
        self.assertEqual([o["name"] for o in self.data["overlays"]], OVERLAYS)
        for o in self.data["overlays"]:
            self.assertTrue(o["round_trip"], o["name"])
        self.assertEqual(self.data["overlay_bad"],
                         {"unknown_found": False, "untouched": True, "out_of_range": None})


if __name__ == "__main__":
    unittest.main()

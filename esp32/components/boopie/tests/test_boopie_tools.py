# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0

"""What the board can do for an AI (tools/boopie_tools_spec.c,
docs/boopie-tools.md), listed once: as Muse's commands and as 小智's MCP
tools, the same tools both ways.

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
CJSON = COMPONENT.parent.parent / "managed_components" / "espressif__cjson" / "cJSON"


@unittest.skipUnless((CJSON / "cJSON.c").exists(), "cJSON isn't fetched (idf.py reconfigure)")
class BoopieToolsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "boopie_tools_harness"
            cc = shlex.split(os.environ.get("CC", "cc"))
            subprocess.run(cc + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-Wno-error=format-truncation",
                                 "-I", str(COMPONENT / "tools"), "-I", str(COMPONENT / "xiaozhi"), "-I", str(CJSON),
                                 str(HERE / "boopie_tools_harness.c"), str(COMPONENT / "tools" / "boopie_tools_spec.c"),
                                 str(COMPONENT / "xiaozhi" / "boopie_xz_proto.c"), str(CJSON / "cJSON.c"), "-lm",
                                 "-o", str(exe)], check=True)
            out = subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout
        cls.r = json.loads(out)

    def test_the_same_tools_both_ways(self) -> None:
        muse, mcp = self.r["muse"], self.r["mcp"]
        self.assertEqual(sorted("self." + k for k in muse), sorted(t["name"] for t in mcp))
        for t in mcp:
            m = muse[t["name"][5:]]
            self.assertEqual(m["description"], t["description"])
            props = t["inputSchema"]["properties"]
            self.assertEqual(set(props), set(m["required"]) | set(m["optional"]))
            self.assertEqual(set(t["inputSchema"].get("required", [])), set(m["required"]))
            for p in props.values():
                self.assertIn(p["type"], ("string", "integer", "boolean"))
                self.assertTrue(p["description"])

    def test_what_muse_had_is_still_there(self) -> None:
        muse = self.r["muse"]
        for name in ("pet.status", "pet.name", "display.avatar", "game.start", "storage.clear", "noise.play",
                     "noise.stop", "garden.status", "world.weather"):
            self.assertIn(name, muse)
        self.assertIn("name", muse["pet.name"]["optional"])
        self.assertIn("what", muse["storage.clear"]["required"])
        self.assertIn("kind", muse["world.weather"]["required"])
        self.assertEqual(set(muse["display.avatar"]["optional"]),
                         {"avatar", "colour", "expression", "reaction", "background", "skin", "accessory", "on"})

    def test_the_new_ones(self) -> None:
        muse = self.r["muse"]
        for name in ("pet.feed", "app.open", "device.sound", "device.brightness", "device.battery"):
            self.assertIn(name, muse)
        self.assertEqual(muse["device.brightness"]["optional"]["brightness"]["minimum"], 10)
        self.assertEqual(muse["device.sound"]["optional"]["volume"]["maximum"], 100)
        self.assertIn("app", muse["app.open"]["required"])
        # Nothing that can't be undone by asking again: no restart, no reset.
        self.assertFalse([n for n in muse if "reset" in n or "restart" in n or "reboot" in n])

    def test_it_fits_one_page(self) -> None:
        # Upstream's MCP server pages tools/list at 8000 bytes; ours is one page.
        self.assertLess(len(json.dumps(self.r["mcp"], ensure_ascii=False).encode()), 8000)
        self.assertGreater(self.r["reply_len"], 0)
        self.assertEqual((self.r["self_found"], self.r["bad_found"]), (1, 0))


if __name__ == "__main__":
    unittest.main()

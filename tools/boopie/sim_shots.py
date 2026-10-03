#!/usr/bin/env python3
# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0

"""Renders each avatar state in the UI simulator and saves PNGs plus contact
sheets, so UI changes can be checked from CI without a board: Muse's own
character in every state (contact-sheet.png), every character in a few
(avatars.png), the pet expressions and overlays on Boopie and Muse (pets.png),
and both in every background (scenes.png).

  python3 tools/boopie/sim_shots.py --binary build/simulator/muse_simulator --out shots

Needs Pillow. Build the simulator with -DMUSE_SIM_WAVESHARE_175C=ON for the
466 x 466 round screen.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw

# (file name, face, extra scenario lines[, environment]).
SHOTS = [
    ("boot", "boot", []),
    ("idle", "idle", []),
    ("listening", "listening", ["level=0.6", "progress=0.4"]),
    ("thinking", "thinking", ["progress=0.5"]),
    ("speaking", "speaking", ["level=0.5", "progress=0.6", "caption=Hi, I'm Boopie! Nice to meet you."]),
    ("speaking-zh", "speaking", ["level=0.5", "progress=0.6",
                                 "caption=你好，我是布比！今天天气晴，最高气温25℃，适合出去走走。"]),
    ("speaking-mixed", "speaking", ["level=0.5", "progress=0.6",
                                    "caption=我会说中文，也会说 English。“布比”——你的小伙伴……"]),
    ("happy", "happy", []),
    ("error", "error", ["caption=CAN'T REACH MUSE"]),
    ("off", "off", []),
]

AVATARS = ["muse", "boopie", "gpt", "codex", "klaude", "whale", "doubao"]

# Every character in these: (name, face, extra lines, ms to run first).
AVATAR_STATES = [
    ("idle", "idle", [], 600),
    ("listening", "listening", ["level=0.6", "progress=0.4"], 600),
    ("thinking", "thinking", ["progress=0.5"], 600),
    ("working", "thinking", ["progress=0.5"], 3000),
    ("speaking", "speaking", ["level=0.5", "progress=0.6", "caption=你好，我是布比！"], 600),
    ("happy", "happy", [], 300),
]

# Boopie's pet expressions and overlays: (name, environment).
PETS = [(e, {"BOOPIE_PET": e}) for e in ("hungry", "eating", "sleepy", "sad", "dizzy")] + \
       [(o, {"BOOPIE_OVERLAY": o}) for o in ("surprise", "blush", "confetti", "hearts")] + \
       [("low_battery", {"BATTERY": "9"}), ("charging", {"CHARGING": "1"})]

SCENES = ["default", "stars", "fireflies", "snow", "petals", "bubbles", "matrix", "neon_grid", "glitch"]

COMMON = [
    "battery=72",
    "usb=false",
    "wifi=connected",
    "ble=connected",
    "paired=true",
    "link=online",
]


def shoot(binary: Path, tmp: Path, png: Path, lines: list[str], env_extra: dict | None = None,
          advance: int = 600) -> Path:
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", **(env_extra or {}))
    common = list(COMMON)
    if env.pop("CHARGING", None):
        common = [c for c in common if not c.startswith("usb=")] + ["usb=true", "charging=true"]
    if "BATTERY" in env:
        common = [c for c in common if not c.startswith("battery=")] + [f"battery={env.pop('BATTERY')}"]
    scenario = tmp / f"{png.stem}.txt"
    scenario.write_text("\n".join([*lines, *common, f"advance={advance}"]) + "\n", encoding="utf-8")
    ppm = tmp / f"{png.stem}.ppm"
    subprocess.run([str(binary), "--headless", "--scenario", str(scenario),
                    "--run-ms", "300", "--screenshot", str(ppm)],
                   check=True, env=env, stdout=subprocess.DEVNULL)
    Image.open(ppm).convert("RGB").save(png)
    return png


def render(binary: Path, out: Path) -> list[Path]:
    with tempfile.TemporaryDirectory() as tmp:
        return [shoot(binary, Path(tmp), out / f"{name}.png", [f"face={face}", *extra])
                for name, face, extra in SHOTS]


def render_avatars(binary: Path, out: Path) -> list[Path]:
    pngs = []
    with tempfile.TemporaryDirectory() as tmp:
        for avatar in AVATARS:
            for name, face, extra, advance in AVATAR_STATES:
                pngs.append(shoot(binary, Path(tmp), out / f"{avatar}-{name}.png", [f"face={face}", *extra],
                                  {"BOOPIE_AVATAR": avatar}, advance))
    return pngs


def render_scenes(binary: Path, out: Path) -> list[Path]:
    with tempfile.TemporaryDirectory() as tmp:
        return [shoot(binary, Path(tmp), out / f"scene-{avatar}-{scene}.png", ["face=idle"],
                      {"BOOPIE_AVATAR": avatar, "BOOPIE_SCENE": scene}, 1000)
                for avatar in ("muse", "boopie") for scene in SCENES]


def render_pets(binary: Path, out: Path) -> list[Path]:
    with tempfile.TemporaryDirectory() as tmp:
        return [shoot(binary, Path(tmp), out / f"pet-{avatar}-{name}.png", ["face=idle"],
                      {"BOOPIE_AVATAR": avatar, **env}, 1000)
                for avatar in ("boopie", "muse") for name, env in PETS]


def contact_sheet(pngs: list[Path], dest: Path, cols: int = 5) -> None:
    ims = [Image.open(p) for p in pngs]
    w, h = ims[0].size
    pad, label = 10, 24
    rows = (len(ims) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (w + pad) + pad, rows * (h + label + pad) + pad), (24, 24, 24))
    draw = ImageDraw.Draw(sheet)
    for i, (p, im) in enumerate(zip(pngs, ims)):
        x = pad + (i % cols) * (w + pad)
        y = pad + (i // cols) * (h + label + pad)
        sheet.paste(im, (x, y))
        draw.text((x + w // 2 - 3 * len(p.stem), y + h + 6), p.stem, fill=(220, 220, 220))
    sheet.save(dest)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--binary", type=Path, required=True, help="muse_simulator executable")
    ap.add_argument("--out", type=Path, required=True, help="directory for the PNGs")
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    pngs = render(args.binary, args.out)
    contact_sheet(pngs, args.out / "contact-sheet.png")
    avatars = render_avatars(args.binary, args.out)
    contact_sheet(avatars, args.out / "avatars.png", cols=len(AVATAR_STATES))
    pets = render_pets(args.binary, args.out)
    contact_sheet(pets, args.out / "pets.png", cols=6)
    scenes = render_scenes(args.binary, args.out)
    contact_sheet(scenes, args.out / "scenes.png", cols=len(SCENES))
    print(f"{len(pngs) + len(avatars) + len(pets) + len(scenes)} screenshots in {args.out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())

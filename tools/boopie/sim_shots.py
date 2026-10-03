#!/usr/bin/env python3
# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0

"""Renders each avatar state in the UI simulator and saves PNGs plus a contact
sheet, so UI changes can be checked from CI without a board.

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

# (file name, face, extra scenario lines).
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

COMMON = [
    "battery=72",
    "usb=false",
    "wifi=connected",
    "ble=connected",
    "paired=true",
    "link=online",
]


def render(binary: Path, out: Path) -> list[Path]:
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    pngs = []
    with tempfile.TemporaryDirectory() as tmp:
        for name, face, extra in SHOTS:
            scenario = Path(tmp) / f"{name}.txt"
            scenario.write_text("\n".join([f"face={face}", *COMMON, *extra, "advance=600"]) + "\n",
                                encoding="utf-8")
            ppm = Path(tmp) / f"{name}.ppm"
            subprocess.run([str(binary), "--headless", "--scenario", str(scenario),
                            "--run-ms", "300", "--screenshot", str(ppm)],
                           check=True, env=env, stdout=subprocess.DEVNULL)
            png = out / f"{name}.png"
            Image.open(ppm).convert("RGB").save(png)
            pngs.append(png)
    return pngs


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
    print(f"{len(pngs)} screenshots in {args.out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())

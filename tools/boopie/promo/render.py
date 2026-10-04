#!/usr/bin/env python3
# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0
"""Renders the promo video: web/index.html frame by frame in headless Chromium,
then ffmpeg with the music (music.py) into an MP4.

  python3 tools/boopie/promo/capture.py --binary <sim> --out <work>/clips
  python3 tools/boopie/promo/render.py --work <work> --fonts <dir with local.css> [--stills 1,20,40]
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def stage(work: Path, fonts: Path) -> Path:
    site = work / "site"
    if site.exists():
        shutil.rmtree(site)
    shutil.copytree(HERE / "web", site)
    (site / "clips").symlink_to((work / "clips").resolve())
    (site / "fonts").symlink_to(fonts.resolve())
    counts = {d.name: len(list(d.glob("*.png"))) for d in sorted((work / "clips").iterdir()) if d.is_dir()}
    skins = [n[5:] for n in counts if n.startswith("skin_")]
    (site / "clips.js").write_text(f"const CLIPS = {json.dumps(counts)};\nconst SKINS = {json.dumps(skins)};\n")
    return site


def main() -> None:
    a = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    a.add_argument("--work", type=Path, required=True)
    a.add_argument("--fonts", type=Path, required=True)
    a.add_argument("--stills", help="comma-separated seconds: only these, as PNGs")
    a.add_argument("--fps", type=int, default=30)
    a.add_argument("--start", type=float, default=0)
    a.add_argument("--end", type=float, default=75)
    args = a.parse_args()
    site = stage(args.work, args.fonts)
    from playwright.sync_api import sync_playwright
    frames = args.work / "frames"
    with sync_playwright() as p:
        b = p.chromium.launch(args=["--allow-file-access-from-files"])
        pg = b.new_page(viewport={"width": 1920, "height": 1080})
        pg.on("pageerror", lambda e: print("page error:", e, file=sys.stderr))
        pg.goto((site / "index.html").as_uri())
        pg.evaluate("window.ready")
        if args.stills:
            for s in args.stills.split(","):
                pg.evaluate(f"renderAt({float(s)})")
                pg.locator("canvas").screenshot(path=str(args.work / f"still_{float(s):05.1f}.png"))
            b.close()
            return
        if frames.exists() and args.start == 0:
            shutil.rmtree(frames)
        frames.mkdir(exist_ok=True)
        n0, n1 = round(args.start * args.fps), round(args.end * args.fps)
        for i in range(n0, n1):
            pg.evaluate(f"renderAt({i / args.fps})")
            data = pg.evaluate("canvas.toDataURL('image/jpeg', 0.95)")
            import base64
            (frames / f"{i:05d}.jpg").write_bytes(base64.b64decode(data.split(",", 1)[1]))
            if i % 150 == 0:
                print(f"{i / args.fps:.0f}s", flush=True)
        b.close()
    out = args.work / "boopie-promo.mp4"
    music = args.work / "music.wav"
    cmd = ["ffmpeg", "-y", "-loglevel", "error", "-framerate", str(args.fps), "-start_number", "0",
           "-i", str(frames / "%05d.jpg")]
    if music.exists():
        cmd += ["-i", str(music), "-c:a", "aac", "-b:a", "192k", "-shortest"]
    cmd += ["-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "18", "-preset", "slow", "-movflags", "+faststart", str(out)]
    subprocess.run(cmd, check=True)
    print(out)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0
"""Records the screen clips for the promo video from the UI simulator: each
clip is a run of frames (record=DIR,COUNT,MS), saved as PNGs.

  python3 tools/boopie/promo/capture.py --binary build/sim/muse_simulator --out promo/clips [names...]
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import sim_shots  # noqa: E402

COMMON = sim_shots.COMMON
B = {"BOOPIE_AVATAR": "boopie", "TZ": "UTC+1", "BOOPIE_WEATHER": "sunny", "BOOPIE_PET_FED": "1",
     "BOOPIE_PET_STARS": "1280", "BOOPIE_PET_XP": "40000"}
NEST = ["swipe=up", "advance=4800"]


def av(name, **kw):
    return {**B, "BOOPIE_AVATAR": name, **kw}


# name: (environment, lines before recording, frames, ms apart)
CLIPS = {
    "boopie_idle": (B, ["face=idle", "advance=800"], 60, 66),
    "boopie_happy": (B, ["face=happy", "advance=100"], 30, 66),
    "boopie_hearts": (dict(B, BOOPIE_OVERLAY="hearts"), ["face=idle", "advance=300"], 40, 66),
    "boopie_confetti": (dict(B, BOOPIE_OVERLAY="confetti"), ["face=happy", "advance=100"], 40, 66),
    "muse_idle": (av("muse"), ["face=idle", "advance=800"], 60, 66),
    "muse_happy": (av("muse"), ["face=happy", "advance=100"], 30, 66),
    "muse_think": (av("muse"), ["face=thinking", "progress=0.5", "advance=300"], 120, 66),
    "muse_speaking": (av("muse"), ["face=speaking", "level=0.6", "progress=0.5",
                                   "caption=你好，我是 Muse！很高兴认识你～", "advance=300"], 45, 66),
    "setup_qr": (B, ["menu=setup", "advance=800"], 15, 66),
    "wifi": (B, ["settings=wifi", "advance=800"], 10, 66),
    "bluetooth": (B, ["settings=bluetooth", "advance=800"], 10, 66),
    "vpn": (dict(B, BOOPIE_VPN="1"), ["settings=vpn", "advance=800"], 10, 66),
    "listening": (B, ["face=listening", "level=0.7", "progress=0.4", "advance=200"], 30, 66),
    "thinking": (B, ["face=thinking", "progress=0.5", "advance=200"], 25, 66),
    "speaking": (B, ["face=speaking", "level=0.6", "progress=0.6",
                     "caption=今天晴，最高 26℃，适合出去走走！", "advance=300"], 45, 66),
    "noise_rain": (B, ["noise=rain", "advance=800"], 40, 66),
    "tool_noise": (B, ["face=idle", "advance=300", 'tool=noise.play:{"kind":"rain","minutes":30}', "advance=100"],
                   30, 66),
    "games_whack": (B, ["game=whack", "advance=500"], 1, 66),
}
for a in ("muse", "boopie", "gpt", "codex", "klaude", "whale", "doubao"):
    CLIPS[f"char_{a}"] = (av(a), ["face=happy", "advance=400", "face=idle", "advance=200"], 24, 66)
for c, k in sim_shots.SKINS:
    CLIPS[f"skin_{k}"] = (av(c, BOOPIE_SKIN=k), ["face=idle", "advance=700"], 16, 66)
for sc in ("stars", "fireflies", "petals", "bubbles", "snow", "default", "neon_grid", "matrix", "glitch"):
    CLIPS[f"muse_think_{sc}"] = (av("muse", BOOPIE_SCENE=sc, TZ="UTC+10"), ["face=thinking", "progress=0.5", "advance=1500"], 60, 66)
for e in ("hungry", "eating", "sleepy", "sad", "dizzy"):
    CLIPS[f"pet_{e}"] = (dict(B, BOOPIE_PET=e), ["face=idle", "advance=600"], 30, 66)
for o in ("surprise", "blush", "confetti", "hearts"):
    CLIPS[f"ov_{o}"] = (dict(B, BOOPIE_OVERLAY=o), ["face=idle", "advance=300"], 30, 66)
for g in ("whack", "catch", "maze", "hop"):
    CLIPS[f"game_{g}"] = (dict(B, BOOPIE_AUTOPLAY="1"), ["face=idle", f"game={g}", "advance=400", "tap=233,233", "advance=3000"], 90, 66)
for room, extra in (("living", []), ("bedroom", []), ("outside", []), ("woods", []), ("beach", [])):
    CLIPS[f"nest_{room}"] = (B, [*NEST, f"room={room}", "advance=1500"], 75, 66)
CLIPS["nest_swim"] = (B, [*NEST, "room=beach", "antic=swim", "advance=1200"], 60, 66)
CLIPS["nest_rain"] = (dict(B, BOOPIE_WEATHER="rain"), [*NEST, "room=outside", "advance=1500"],
                      60, 66)
CLIPS["nest_xmas"] = (dict(B, BOOPIE_FEST="4", BOOPIE_WEATHER="snow"),
                      [*NEST, "room=outside", "advance=3000"], 90, 66)
CLIPS["nest_slime"] = (B, [*NEST, "room=woods", "advance=2500", "tap=415,300",
                                                         "advance=600"], 60, 66)
CLIPS["nest_farm"] = (dict(B, BOOPIE_GARDEN="bloom"), [*NEST, "room=outside", "tap=440,300", "advance=3000"], 60, 66)


def record(binary: Path, out: Path, name: str) -> str:
    env_extra, lines, count, ms = CLIPS[name]
    if sim_shots.ASSETS is None:
        sim_shots.ASSETS = sim_shots.assets_pack()
    d = out / name
    d.mkdir(parents=True, exist_ok=True)
    for f in d.glob("*"):
        f.unlink()
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", BOOPIE_ASSETS=str(sim_shots.ASSETS),
               BOOPIE_DATA=str(out / f".data_{name}"), **env_extra)
    (out / f".data_{name}").mkdir(exist_ok=True)
    scen = out / f".{name}.txt"
    scen.write_text("\n".join([*COMMON, *lines, f"record={d},{count},{ms}"]) + "\n", encoding="utf-8")
    subprocess.run([str(binary), "--headless", "--scenario", str(scen), "--run-ms", "0"], check=True, env=env,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for p in sorted(d.glob("*.ppm")):
        Image.open(p).convert("RGB").save(p.with_suffix(".png"))
        p.unlink()
    return name


def main() -> None:
    a = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    a.add_argument("--binary", type=Path, required=True)
    a.add_argument("--out", type=Path, required=True)
    a.add_argument("names", nargs="*")
    args = a.parse_args()
    sim_shots.ASSETS = sim_shots.assets_pack()
    names = args.names or list(CLIPS)
    with ThreadPoolExecutor(8) as ex:
        for n in ex.map(lambda n: record(args.binary, args.out, n), names):
            print(n, flush=True)


if __name__ == "__main__":
    main()

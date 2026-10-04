#!/usr/bin/env python3
# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0

"""Renders each avatar state in the UI simulator and saves PNGs plus contact
sheets, so UI changes can be checked from CI without a board: Muse's own
character in every state (contact-sheet.png), every character in a few
(avatars.png), the pet expressions and overlays on Boopie and Muse (pets.png),
both in every background (scenes.png), and each skin beside its
character without it (skins.png).

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
import time
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
    ("error", "error", ["caption=连不上 Muse"]),
    ("battery-low", "idle", ["advance=1200"], {"BATTERY": "15"}),
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


ROOT = Path(__file__).resolve().parents[2]


def assets_pack() -> Path:
    """esp32/assets packed as the device has it, for the simulator (BOOPIE_ASSETS)."""
    out = Path(tempfile.gettempdir()) / "boopie-sim-assets.bin"
    subprocess.run([sys.executable, str(ROOT / "tools" / "boopie" / "pack_assets.py"), "--out", str(out)],
                   check=True, stdout=subprocess.DEVNULL)
    return out


ASSETS = None


def shoot(binary: Path, tmp: Path, png: Path, lines: list[str], env_extra: dict | None = None,
          advance: int = 600) -> Path:
    global ASSETS
    if ASSETS is None:
        ASSETS = assets_pack()
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", BOOPIE_ASSETS=str(ASSETS),
               **(env_extra or {}))
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
        return [shoot(binary, Path(tmp), out / f"{name}.png", [f"face={face}", *shot[2]], *shot[3:])
                for shot in SHOTS for name, face in [shot[:2]]]


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


SKINS = [("boopie", "boopie_starry"), ("boopie", "boopie_jelly"), ("muse", "muse_astronaut"),
         ("muse", "muse_matcha"), ("gpt", "gpt_ink"), ("gpt", "gpt_porcelain"), ("codex", "codex_neon"),
         ("codex", "codex_glitch"), ("klaude", "klaude_ice"), ("klaude", "klaude_lava"), ("whale", "whale_koi"),
         ("whale", "whale_deepsea"), ("doubao", "doubao_sakura"), ("doubao", "doubao_winter")]
SKIN_STATES = [s for s in AVATAR_STATES if s[0] in ("idle", "thinking", "speaking", "happy")]


def render_skins(binary: Path, out: Path) -> list[Path]:
    """Each skin in a few states (the characters without one are in avatars.png)."""
    pngs = []
    with tempfile.TemporaryDirectory() as tmp:
        for avatar, skin in SKINS:
            for name, face, extra, advance in SKIN_STATES:
                env = {"BOOPIE_AVATAR": avatar, "BOOPIE_SKIN": skin}
                pngs.append(shoot(binary, Path(tmp), out / f"skin-{skin}-{name}.png",
                                  [f"face={face}", *extra], env, advance))
    return pngs


def render_pets(binary: Path, out: Path) -> list[Path]:
    with tempfile.TemporaryDirectory() as tmp:
        return [shoot(binary, Path(tmp), out / f"pet-{avatar}-{name}.png", ["face=idle"],
                      {"BOOPIE_AVATAR": avatar, **env}, 1000)
                for avatar in ("boopie", "muse") for name, env in PETS]


WEARS = ["bow,scarf", "crown", "party_hat,scarf"]


def render_accessories(binary: Path, out: Path) -> list[Path]:
    """Every character in each accessory, idle."""
    with tempfile.TemporaryDirectory() as tmp:
        return [shoot(binary, Path(tmp), out / f"wear-{avatar}-{wear.replace(',', '+')}.png", ["face=idle"],
                      {"BOOPIE_AVATAR": avatar, "BOOPIE_WEAR": wear}, 600)
                for avatar in AVATARS for wear in WEARS]


# The pages round the face: a finger swiping right shows apps, left settings,
# down (pulling from the top) the cards, up the pet; and the power menu.
PAGES = [("page-home", []), ("page-apps", ["swipe=right"]), ("page-settings", ["swipe=left"]),
         ("page-cards", ["swipe=down"]), ("page-pet", ["swipe=up"]), ("power-menu", ["menu=power"]),
         ("game-ready", ["game=whack"]), ("game-play", ["game=whack", "tap=233,233", "advance=20000"]),
         ("game-over", ["game=whack", "tap=233,233", "advance=61000"]),
         ("input-pinyin", ["input=", "tap=233,284", "tap=335,234"]),   # keys 6 then 4: ni, mi ...
         ("input-typed", ["input=", "tap=233,284", "tap=335,234", "tap=86,184"])]
# The setup guide (BOOPIE_GUIDE): its first page, and choosing the pet.
GUIDE = [("guide-hello", []), ("guide-online", ["tap=233,386", "advance=200"]),
         ("guide-pet", ["tap=233,386", "advance=200", "tap=233,386", "advance=200", "tap=233,324", "advance=200",
                        "tap=233,386", "advance=200"])]


def render_pages(binary: Path, out: Path) -> list[Path]:
    with tempfile.TemporaryDirectory() as tmp:
        return [shoot(binary, Path(tmp), out / f"{name}.png", ["face=idle", "advance=300", *steps],
                      {"BOOPIE_PET_XP": "900"}, 800)
                for name, steps in PAGES]


def render_guide(binary: Path, out: Path) -> list[Path]:
    with tempfile.TemporaryDirectory() as tmp:
        return [shoot(binary, Path(tmp), out / f"{name}.png", ["face=idle", "advance=300", *steps],
                      {"BOOPIE_GUIDE": "1"}, 400)
                for name, steps in GUIDE]


# Phone setup: the hotspot's QR code, the page's once a phone has joined, and
# what the page saved (all five).
PHONE = [("setup-join", {}), ("setup-open", {"BOOPIE_SETUP_CLIENTS": "1"}),
         ("setup-saved", {"BOOPIE_SETUP_CLIENTS": "1", "BOOPIE_SETUP_SAVED": "31"})]


def render_phone(binary: Path, out: Path) -> list[Path]:
    with tempfile.TemporaryDirectory() as tmp:
        return [shoot(binary, Path(tmp), out / f"{name}.png", ["face=idle", "advance=300", "menu=setup", "advance=300"],
                      env, 400)
                for name, env in PHONE]


# Looking back: the chat history, the album and the box asking before a clear,
# over a user data folder with a few turns and a picture in it.
LOOK = [("viewer-chat", ["viewer=chat"]), ("viewer-album", ["viewer=album"]), ("ask-clear", ["ask=chat"]),
        ("settings-storage", ["settings=storage", "advance=600"]),
        ("apps-scrolled", ["swipe=right", "advance=600", "swipe=up", "advance=1500"]),
        ("game-catch", ["game=catch", "tap=233,233", "advance=9000"]),
        ("game-maze", ["game=maze", "tap=233,233", "advance=3000"]),
        ("noise", ["noise=rain", "advance=3000"]),
        ("game-hop", ["game=hop", *["tap=233,233"] * 4]),
        ("pet-stroke", ["stroke=4"]), ("pet-hug", ["stroke=0"]),
        ("nest", ["swipe=up", "advance=1500"]), ("nest-shop", ["swipe=up", "advance=1200", "tap=307,418"])]
# 小窝's yard and farm, and the woods (BOOPIE_GARDEN=bloom: a sunflower in bud, a dry tulip, a
# strawberry to pick): out of the door, walked over to the plots, and the
# seeds for an empty one.
FARM = [("nest-outside", ["swipe=up", "advance=600", "room=outside", "advance=1500"]),
        ("nest-farm", ["swipe=up", "advance=600", "room=outside", "tap=440,300", "advance=6000"]),
        ("nest-seeds", ["swipe=up", "advance=600", "room=outside", "tap=440,300", "advance=6000",
                        "tap=233,325", "advance=4000"]),
        # 森林: in by the sign, and a slime fought (its hits and the time over it).
        ("nest-woods", ["swipe=up", "advance=600", "room=woods", "advance=2500"]),
        ("nest-slime", ["swipe=up", "advance=600", "room=woods", "advance=2500", "tap=415,300", "advance=3000"]),
        ("nest-beach", ["swipe=up", "advance=600", "room=beach", "advance=2500"]),
        ("nest-swim", ["swipe=up", "advance=600", "room=beach", "antic=swim", "advance=5000"]),
        ("nest-tv", ["swipe=up", "advance=600", "room=living", "antic=tv", "advance=5000"])]


def user_data(root: Path) -> Path:
    data = root / "data"
    for d in ("chat", "album", "notes", "logs", "games"):
        (data / d).mkdir(parents=True, exist_ok=True)
    turns = [("今天天气怎么样？", "今天晴，最高 25℃，适合出去走走。\\n记得带水哦。"),
             ("给布比起个名字叫小白", "好呀，以后我就叫小白啦！"),
             ("What's 12 times 12?", "12 times 12 is 144.")]
    (data / "chat" / "log.txt").write_text(
        "".join(f"{1759480000 + i * 3600}\t{q}\t{a}\n" for i, (q, a) in enumerate(turns)), encoding="utf-8")
    im = Image.new("RGB", (480, 360), (40, 32, 80))
    draw = ImageDraw.Draw(im)
    for i in range(12):
        draw.ellipse((20 + i * 36, 120 + (i % 3) * 40, 70 + i * 36, 170 + (i % 3) * 40),
                     fill=(255, 120 + i * 10, 180 - i * 8))
    im.save(data / "album" / "1759480000.jpg", "JPEG", quality=85, progressive=False)
    (data / "notes" / f"{int(time.time()) - 600:010d}.pcm").write_bytes(b"\0\0" * 16000)
    return data


def render_look(binary: Path, out: Path) -> list[Path]:
    with tempfile.TemporaryDirectory() as tmp:
        data = user_data(Path(tmp))
        return [shoot(binary, Path(tmp), out / f"{name}.png", ["face=idle", "advance=300", *steps],
                      {"BOOPIE_DATA": str(data), "BOOPIE_VPN": "1"}, 400)
                for name, steps in LOOK]


# Furniture from the shop, all of it out (BOOPIE_FURNI), the shop's furniture
# page, and the bag with what the woods gave (BOOPIE_BAG).
FURNITURE = [("nest-furniture", ["swipe=up", "advance=600", "room=bedroom", "advance=300"]),
             ("nest-shop-furniture", ["swipe=up", "advance=600", "tap=307,418", "advance=600", "tap=233,165"]),
             ("nest-bag", ["swipe=up", "advance=600", "tap=233,418", "advance=600"])]


# Weather and festivals (BOOPIE_WEATHER, BOOPIE_FEST as boopie_fest_t): rain
# with its umbrella, snow and its snowman, the Spring Festival's lanterns.
SEASONS = [("nest-rain", {"BOOPIE_WEATHER": "rain"}), ("nest-snow", {"BOOPIE_WEATHER": "snow"}),
           ("nest-spring", {"BOOPIE_FEST": "1"}), ("nest-xmas", {"BOOPIE_FEST": "4", "BOOPIE_WEATHER": "snow"})]


def render_seasons(binary: Path, out: Path) -> list[Path]:
    with tempfile.TemporaryDirectory() as tmp:
        return [shoot(binary, Path(tmp), out / f"{name}.png",
                      ["face=idle", "advance=300", "swipe=up", "advance=600", "room=outside", "advance=2500"],
                      {"BOOPIE_PET_XP": "900", **env}, 400)
                for name, env in SEASONS]


def render_furniture(binary: Path, out: Path) -> list[Path]:
    with tempfile.TemporaryDirectory() as tmp:
        return [shoot(binary, Path(tmp), out / f"{name}.png", ["face=idle", "advance=300", *steps],
                      {"BOOPIE_FURNI": "5", "BOOPIE_BAG": "1", "BOOPIE_PET_XP": "900"}, 400)
                for name, steps in FURNITURE]


def render_farm(binary: Path, out: Path) -> list[Path]:
    with tempfile.TemporaryDirectory() as tmp:
        return [shoot(binary, Path(tmp), out / f"{name}.png", ["face=idle", "advance=300", *steps],
                      {"BOOPIE_GARDEN": "bloom", "BOOPIE_PET_XP": "900"}, 400)
                for name, steps in FARM]


# The settings pages, reached by a scripted finger (466 px screen).
SETTINGS = [
    ("settings-home", ["swipe=left"]),
    ("settings-wifi", ["settings=wifi", "advance=600"]),
    ("settings-wifi-password", ["settings=wifi", "advance=600", "tap=230,459", "advance=600"]),
    ("settings-avatar", ["settings=avatar", "advance=600"]),
    *[(f"settings-{page}", [f"settings={page}", "advance=600"])
      for page in ("brain", "xiaozhi", "muse", "vpn", "bluetooth", "sound", "sleep", "battery")],
]


def render_settings(binary: Path, out: Path) -> list[Path]:
    with tempfile.TemporaryDirectory() as tmp:
        return [shoot(binary, Path(tmp), out / f"{name}.png", ["face=idle", "advance=300", *steps],
                      {"BOOPIE_PET_XP": "900", "BOOPIE_VPN": "1"})
                for name, steps in SETTINGS]


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
    skins = render_skins(args.binary, args.out)
    contact_sheet(skins, args.out / "skins.png", cols=len(SKIN_STATES) * 2)
    pages = render_pages(args.binary, args.out)
    pages += render_guide(args.binary, args.out)
    contact_sheet(pages, args.out / "pages.png", cols=len(PAGES))
    pages += render_phone(args.binary, args.out)
    pages += render_look(args.binary, args.out)
    pages += render_farm(args.binary, args.out)
    pages += render_furniture(args.binary, args.out)
    pages += render_seasons(args.binary, args.out)
    settings = render_settings(args.binary, args.out)
    contact_sheet(settings, args.out / "settings.png", cols=len(SETTINGS))
    wears = render_accessories(args.binary, args.out)
    contact_sheet(wears, args.out / "accessories.png", cols=len(WEARS) * 2)
    print(f"{len(pngs) + len(avatars) + len(pets) + len(scenes) + len(skins) + len(wears) + len(settings) + len(pages)} screenshots in {args.out}",
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())

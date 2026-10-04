#!/usr/bin/env python3
# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0

"""Boopie's pixel avatars, as a design prototype: draws every expression in
esp32/components/boopie/boopie_expr.h for each character on the same 64 x 64
grid the firmware uses, and writes previews.

  python3 tools/boopie/avatar_proto.py --out previews/
  python3 tools/boopie/avatar_proto.py --out previews/ --character boopie --color 7fe3c4

writes, per character, one GIF per expression plus all.gif (every expression,
animated) and sheet.png (a frame of each), and family.gif with every
character side by side. The firmware renderer will be a C port of this file.
Needs Pillow and NumPy.

One expression engine drives every character: an expression is a Pose (how
the body moves, which eyes and mouth, where the hands go, the effects round
it and the colour of the state light). A character (Rig) only draws its own
body, decides how its face is drawn and where its state light lives.
"""

from __future__ import annotations

import argparse
import colorsys
import math
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

N = 64
FPS = 12
BAYER = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]) / 16.0
YS, XS = np.mgrid[0:N, 0:N] + 0.5

EYE = (30, 22, 46)
WHITE = (255, 255, 255)
CHEEK = (255, 122, 152)
TEAR = (120, 190, 255)
GOLD = (255, 206, 84)
RED = (255, 84, 96)

# The state light's colour; None takes the character's own.
LIGHT = {
    "boot": WHITE, "idle": None, "listening": (110, 190, 255), "thinking": (200, 130, 255),
    "speaking": (110, 235, 170), "error": RED, "off": (90, 84, 110), "happy": (255, 222, 90),
    "hungry": (255, 170, 80), "eating": (255, 190, 110), "sleepy": (120, 112, 170),
    "sad": (130, 160, 230), "surprised": WHITE, "dizzy": (255, 200, 120), "celebrate": None,
    "shy": (255, 140, 180), "low_battery": RED, "charging": (255, 230, 90), "working": (110, 235, 170),
}

# The state's accent, as Muse's official schemes: the aura, rim light,
# sparkles and rings round the character take it.
ACCENT = {
    "boot": (169, 192, 255), "idle": (167, 125, 255), "listening": (92, 184, 255),
    "thinking": (224, 123, 255), "speaking": (111, 240, 191), "error": (255, 92, 92),
    "off": (124, 114, 208), "happy": (167, 125, 255), "hungry": (255, 170, 80),
    "eating": (255, 190, 110), "sleepy": (124, 114, 208), "sad": (110, 140, 230),
    "dizzy": (255, 200, 120),
}

# Thinking turns into working (typing on a tiny laptop) after this many
# seconds; the preview loop shows both phases.
WORKING_AFTER = 2.4
WORKING_LOOP = 1.2

# Name, seconds per loop, Chinese label. Order and names as boopie_expr.h.
EXPRESSIONS = [
    ("boot", 2.0, "开机"), ("idle", 3.0, "待机"), ("listening", 1.6, "聆听"),
    ("thinking", WORKING_AFTER + 2 * WORKING_LOOP, "思考"), ("speaking", 1.2, "说话"), ("error", 2.0, "出错"),
    ("off", 2.4, "关机"), ("happy", 1.2, "开心"),
    ("hungry", 2.4, "饿了"), ("eating", 1.2, "吃东西"), ("sleepy", 3.0, "犯困"),
    ("sad", 2.4, "难过"), ("dizzy", 1.6, "晕了"),
]

# Overlays, as boopie_overlay_t: name, seconds per loop, Chinese label.
OVERLAYS = [
    ("surprise", 1.2, "惊讶 !"), ("blush", 2.4, "害羞"), ("confetti", 1.6, "庆祝"),
    ("hearts", 1.2, "爱心"), ("low_battery", 2.0, "低电量"), ("charging", 1.6, "充电中"),
    ("food", 1.6, "饭碗"),
]

# Where the food bowl sits (the food overlay): tap inside to feed.
FOOD_BOX = (46, 42, 63, 59)   # x0, y0, x1, y1, grid cells, inclusive
# What the pet may be offered (icons), a different one each meal, centred on
# the rice bowl's place and bottom-aligned with it.
FOODS = ["rice", "drumstick", "onigiri", "fish", "big_cookie"]


def ramp(hex_colour: str) -> dict:
    r, g, b = (int(hex_colour[i:i + 2], 16) / 255 for i in (0, 2, 4))
    h, l, s = colorsys.rgb_to_hls(r, g, b)

    def c(dl, ds=0.0, dh=0.0):
        rr, gg, bb = colorsys.hls_to_rgb((h + dh) % 1, min(1, max(0, l + dl)), min(1, max(0, s + ds)))
        return (round(rr * 255), round(gg * 255), round(bb * 255))
    return {"out": c(-0.52, 0.1, 0.03), "dark": c(-0.16, 0.05, 0.02), "mid": c(0), "light": c(0.1),
            "high": c(0.2, -0.1), "glow": c(0.16, 0.25, -0.03)}


def mix(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))


def hue_colour(h: float, v: float) -> tuple:
    """A pastel of hue h (0..1) at brightness v: the hue, a third white."""
    r = min(1.0, max(0.0, abs(h * 6 - 3) - 1))
    g = min(1.0, max(0.0, 2 - abs(h * 6 - 2)))
    b = min(1.0, max(0.0, 2 - abs(h * 6 - 4)))
    return tuple(round((c + (1 - c) * 0.35) * v * 255) for c in (r, g, b))


ROLES = ("dark", "mid", "light", "high")


# ---------------------------------------------------------------- icons
ICONS = {
    "heart": ([".##.##.", "#######", "#######", ".#####.", "..###..", "...#..."], {"#": (255, 90, 140)}),
    "z": (["####", "..#.", ".#..", "####"], {"#": (200, 200, 255)}),
    "excl": (["##", "##", "##", "##", "..", "##"], {"#": (255, 230, 90)}),
    "drop": ([".#.", "###", "###", ".#."], {"#": TEAR}),
    "dot": (["##", "##"], {"#": (220, 210, 255)}),
    "note": (["..##", "..#.", "..#.", "###.", "##.."], {"#": (150, 245, 200)}),
    "bowl": (["#.#.#..", ".#.#...", "#######", ".#####.", "..###.."], {"#": (255, 255, 255)}),
    "cookie": ([".###.", "#o##o", "###o#", "#o###", ".###."], {"#": (214, 150, 80), "o": (110, 64, 34)}),
    "battery": (["#######.", "#r....##", "#r....##", "#######."], {"#": (220, 220, 230), "r": RED}),
    "bolt": (["..##", ".##.", "####", ".##.", "##.."], {"#": (255, 230, 90)}),
    "bubble": ([".#####.", "#.....#", "#.....#", "#.....#", ".#####.", "....#..", ".....#."],
               {"#": (200, 195, 225)}),
    "code": (["#...#", "#.#.#", "#...#"], {"#": (150, 245, 200)}),
    "rice": (["..o...o...o", ".o...o...o.", "wwwwwwwwwww", "#wwwwwwwww#", ".#########.", "..#######..", "...#####..."],
             {"w": (250, 248, 236), "#": (240, 140, 60), "o": (170, 170, 190)}),
    # Food: one shows when the pet's hungry, and the same sprites fall in the snack game.
    "drumstick": ([".ddddd.....", "dbbbbbd....", "dbhhbbbd...", "dbhbbbbd...", "dbbbbbbd...", ".dbbbbdw...",
                   "..ddddww...", "......ww.w.", ".......wwww", "........ww."],
                  {"d": (150, 70, 30), "b": (214, 120, 50), "h": (250, 190, 110), "w": (245, 240, 225)}),
    "onigiri": (["....ww....", "...wwww...", "..wwwwww..", ".wwwwwwww.", "wwwnnnnwww", "wwwnnnnwww", ".wwnnnnww."],
                {"w": (250, 248, 236), "n": (40, 70, 50)}),
    "fish": (["...ffff....f", "..ffffff..ff", ".fefffffffff", ".fffffffffff", "..llllll..ff", "...llll....f"],
             {"f": (90, 160, 255), "l": (190, 225, 255), "e": (20, 20, 40)}),
    "big_cookie": (["..#####..", ".##o####.", "#####o###", "##o######", "####o##o#", ".#######.", "..#####.."],
                   {"#": (214, 150, 80), "o": (110, 64, 34)}),
    "candy": (["p.......p", "pp.ryr.pp", "ppryryrpp", "pp.ryr.pp", "p.......p"],
              {"p": (255, 150, 200), "r": (255, 90, 120), "y": (255, 230, 120)}),
    "laptop": (["..##########..", ".############.", ".#####oo#####.", ".############.",
                ".############.", "ssssssssssssss", ".kkkkkkkkkkkk."],
               {"#": (70, 66, 96), "o": (150, 245, 200), "s": (160, 156, 186), "k": (110, 106, 136)}),
}


@dataclass
class Pose:
    t: float = 0.0
    dx: float = 0.0            # body offset
    dy: float = 0.0
    squash: float = 1.0        # > 1 wider and shorter
    scale: float = 1.0
    tilt: float = 0.0          # x shear per row above the feet
    feet: bool = True
    hands: tuple = ((-1, 3), (1, 3))   # (side, dy) at the body's edge, or ("front", "face"|"body", dx, dy)
    antenna: float = 18.0      # degrees right of vertical: Boopie's antenna, the whale's tail
    antenna_len: float = 9.0
    light: tuple | None = None
    light_level: float = 1.0
    eyes: str = "open"         # open blink happy wide half spiral x worried down look
    look: tuple = (0, 0)
    mouth: str = "w"           # w smile o talk frown wavy flat chomp
    talk: int = 1
    blush: str = "normal"      # normal big none
    spin: int = 0              # spiral eye phase
    prop: str = ""             # laptop
    accent: tuple = (167, 125, 255)
    aura: float = 0.75         # strength of the glow round the character
    rings: float = 0.0         # speed of the dotted rings (listening, speaking); 0 for none
    level: float = 0.3         # voice level, 0..1: the aura and rings swell with it
    sparkle_speed: float = 0.6
    fx: list = field(default_factory=list)
    dim: float = 1.0
    wear: tuple = ()           # accessories (ACCESSORIES)
    scene: str = "default"     # the idle background (SCENES)


# ---------------------------------------------------------------- drawing
class Canvas:
    def __init__(self):
        self.img = np.zeros((N, N, 3), dtype=np.uint8)
        self.eye = EYE
        self.shine = WHITE
        self.rim = None            # state-tinted rim light on the lower right edge, as Muse's
        self.body = np.zeros((N, N), dtype=bool)

    def put(self, x, y, colour):
        x, y = int(round(x)), int(round(y))
        if 0 <= x < N and 0 <= y < N:
            self.img[y, x] = colour

    def shaded(self, mask, rp, light=(-0.55, -0.83)):
        """Fill mask with ramp rp, lit from the top left by the slope of its blur, dithered."""
        if not mask.any():
            return
        h = mask.astype(float)
        for _ in range(3):
            p = np.pad(h, 2, mode="edge")
            h = sum(p[dy:dy + N, dx:dx + N] for dy in range(5) for dx in range(5)) / 25
        gy, gx = np.gradient(h)
        d = (-gx * light[0] - gy * light[1]) * 10 + (h - 0.95)
        d += (BAYER[np.arange(N)[:, None] % 4, np.arange(N)[None, :] % 4] - 0.5) * 0.18
        for role, cond in (("dark", d <= -0.3), ("mid", (d > -0.3) & (d <= 0.2)),
                           ("light", (d > 0.2) & (d <= 0.55)), ("high", d > 0.55)):
            self.img[mask & cond] = rp[role]
        if self.rim is not None:
            bay = BAYER[np.arange(N)[:, None] % 4, np.arange(N)[None, :] % 4]
            out = (-gx * 0.6 - gy * 0.8) / (np.hypot(gx, gy) + 1e-6)
            rim = mask & (h < 0.8) & (out > 0.75) & (bay < 0.3)
            self.img[rim] = mix(rp["mid"], self.rim, 0.35)
        self.body |= mask

    def flat(self, mask, colour):
        self.img[mask] = colour

    def outline(self, mask, colour):
        ring = np.zeros_like(mask)
        ring[1:] |= mask[:-1]
        ring[:-1] |= mask[1:]
        ring[:, 1:] |= mask[:, :-1]
        ring[:, :-1] |= mask[:, 1:]
        self.img[ring & ~mask] = colour

    def icon(self, name, x, y):
        rows, colours = ICONS[name]
        for j, row in enumerate(rows):
            for i, ch in enumerate(row):
                if ch in colours:
                    self.put(x + i, y + j, colours[ch])

    def spark(self, x, y, colour=(255, 246, 200), r=1):
        self.put(x, y, colour)
        for k in range(1, r + 1):
            for dx, dy in ((k, 0), (-k, 0), (0, k), (0, -k)):
                self.put(x + dx, y + dy, colour)


class Frame:
    """Maps a character's rest coordinates to the screen for one pose:
    squash and scale about the feet, then shift and shear."""

    def __init__(self, p: Pose, cx: float, base: float, size: float = 1.0, squash_k: float = 1.0,
                 jump_k: float = 1.0):
        if p.dy < 0 and jump_k != 1.0:   # tall characters jump lower, clear of the status line
            p = Pose(**{**p.__dict__, "dy": p.dy * jump_k})
        self.p, self.cx, self.base = p, cx, base
        squash = 1 + (p.squash - 1) * squash_k      # tall characters squash less
        self.sx = squash * p.scale * size
        self.sy = p.scale / squash * size
        # screen -> rest, for masks
        ys = YS - p.dy
        self.ry = base + (ys - base) / self.sy
        self.rx = cx + (XS - p.dx - cx - p.tilt * (ys - base)) / self.sx

    def ellipse(self, cx, cy, rx, ry):
        return ((self.rx - cx) / rx) ** 2 + ((self.ry - cy) / ry) ** 2 <= 1

    def rot_ellipse(self, cx, cy, rx, ry, angle):
        ca, sa = math.cos(angle), math.sin(angle)
        u = (self.rx - cx) * ca + (self.ry - cy) * sa
        v = -(self.rx - cx) * sa + (self.ry - cy) * ca
        return (u / rx) ** 2 + (v / ry) ** 2 <= 1

    def rect(self, x0, y0, x1, y1):
        return (self.rx >= x0) & (self.rx < x1) & (self.ry >= y0) & (self.ry < y1)

    def pt(self, x, y):
        """Rest point -> screen point."""
        sy = self.base + (y - self.base) * self.sy + self.p.dy
        sx = self.cx + (x - self.cx) * self.sx + self.p.dx + self.p.tilt * (sy - self.p.dy - self.base)
        return sx, sy


# ---------------------------------------------------------------- faces
def eye(c: Canvas, ex, ey, p: Pose, side, square=False):
    k, E = p.eyes, c.eye
    if k == "blink":
        for dx in range(-1, 2):
            c.put(ex + dx, ey + 1, E)
    elif k == "happy":
        for dx, dy in ((-2, 1), (-1, 0), (0, -1), (1, 0), (2, 1)):
            c.put(ex + dx, ey + dy, E)
    elif k == "down":
        for dx, dy in ((-2, 0), (-1, 1), (0, 1), (1, 1), (2, 0)):
            c.put(ex + dx, ey + dy, E)
    elif k == "x":
        for d in range(-2, 3):
            c.put(ex + d, ey + d, E)
            c.put(ex + d, ey - d, E)
    elif k == "spiral":
        ring = [(-1, -2), (0, -2), (1, -2), (2, -1), (2, 0), (2, 1), (1, 2), (0, 2), (-1, 2), (-2, 1), (-2, 0), (-2, -1)]
        gap = (p.spin * 3 + (side > 0) * 6) % len(ring)
        for i, (x, y) in enumerate(ring):
            if i not in (gap, (gap + 1) % len(ring)):
                c.put(ex + x, ey + y, E)
        c.put(ex, ey, E)
    elif k == "half":
        for dx in range(-2, 2):
            c.put(ex + dx, ey, E)
        for dx in range(-1, 2):
            c.put(ex + dx, ey + 1, E)
            c.put(ex + dx, ey + 2, E)
    elif k == "wide":
        for dy in range(-3, 4):
            for dx in range(-2, 2):
                if square or not (abs(dy) == 3 and dx in (-2, 1)):
                    c.put(ex + dx, ey + dy, E)
        if c.shine:
            c.put(ex - 1, ey - 2, c.shine)
            c.put(ex, ey + 1, c.shine)
    else:                 # open / worried / look
        lx, ly = p.look
        for dy in range(-2, 3):
            for dx in range(-1, 2):
                if square or not (abs(dy) == 2 and dx != 0):
                    c.put(ex + dx + lx, ey + dy + ly, E)
        if c.shine:
            c.put(ex - 1 + lx, ey - 1 + ly, c.shine)
        if k == "worried":
            for i in range(3):
                c.put(ex - side * (1 - i), ey - 4 - (1 if i == 0 else 0), E)


def mouth(c: Canvas, mx, my, p: Pose, colour=None, inside=CHEEK):
    k, E = p.mouth, colour or c.eye
    if k == "w":
        for dx, dy in ((-2, 0), (-1, 1), (0, 0), (1, 1), (2, 0)):
            c.put(mx + dx, my + dy, E)
    elif k == "smile":
        for dx, dy in ((-2, 0), (-1, 1), (0, 1), (1, 1), (2, 0)):
            c.put(mx + dx, my + dy, E)
    elif k == "o":
        for dx, dy in ((0, 0), (-1, 1), (1, 1), (0, 2)):
            c.put(mx + dx, my + dy, E)
        c.put(mx, my + 1, inside)
    elif k == "talk":
        h = max(0, min(3, p.talk))
        for dx in range(-1, 2):
            c.put(mx + dx, my, E)
            for dy in range(1, h + 1):
                c.put(mx + dx, my + dy, inside)
        if h:
            for dx in range(-1, 2):
                c.put(mx + dx, my + h + 1, E)
    elif k == "frown":
        for dx, dy in ((-2, 1), (-1, 0), (0, 0), (1, 0), (2, 1)):
            c.put(mx + dx, my + dy, E)
    elif k == "wavy":
        for dx, dy in ((-3, 1), (-2, 0), (-1, 1), (0, 1), (1, 0), (2, 1), (3, 1)):
            c.put(mx + dx, my + dy, E)
    elif k == "flat":
        for dx in range(-1, 2):
            c.put(mx + dx, my + 1, E)
    elif k == "chomp":
        for dx in range(-2, 3):
            c.put(mx + dx, my, E)
        if p.talk:
            for dx in range(-1, 2):
                c.put(mx + dx, my + 1, inside)
                c.put(mx + dx, my + 2, E)


def blush(c: Canvas, fx, fy, p: Pose, dx_cheek=11, colour=CHEEK):
    w = {"normal": 1, "big": 2, "none": 0}[p.blush]
    for side in (-1, 1):
        for dx in range(-w, w + 1):
            if w:
                c.put(fx + side * dx_cheek + dx, fy + 4, colour)
                if w == 2:
                    c.put(fx + side * dx_cheek + dx, fy + 5, colour)


# ---------------------------------------------------------------- characters
class Rig:
    key = ""
    name = ""
    colour = "ff9ec8"
    eye_gap = 7
    size = 1.18        # drawn a little bigger than the rest coordinates, to fill the grid like Muse's
    squash_k = 1.0     # how much of a pose's squash this character takes
    jump_k = 1.0       # how much of a pose's jump

    def frame(self, p, cx, base):
        return Frame(p, cx, base, self.size, self.squash_k, self.jump_k)

    def __init__(self, colour=None, skin=None):
        self.skin = skin
        self.rp = ramp(skin.colour if skin else (colour or self.colour))
        self.eye_colour, self.cheek = EYE, CHEEK
        if skin:
            if skin.glow:
                self.rp["glow"] = skin.glow
            if skin.outline:
                self.rp["out"] = skin.outline
            self.eye_colour = skin.eye or EYE
            self.cheek = skin.cheek or CHEEK
            for k, v in skin.extra.items():
                setattr(self, k, v)

    def draw(self, c: Canvas, p: Pose) -> dict:
        raise NotImplementedError

    mouth_inside = CHEEK

    def face(self, c: Canvas, fx, fy, p: Pose):
        for side in (-1, 1):
            eye(c, fx + side * self.eye_gap, fy, p, side)
        blush(c, fx, fy, p, colour=self.cheek)
        mouth(c, fx + p.look[0] // 2, fy + 5, p, inside=self.mouth_inside)

    def light_colour(self, p: Pose):
        base = p.light or self.rp["glow"]
        return mix((20, 16, 30), base, max(0.0, min(1.0, p.light_level)))

    def hand_ramp(self):
        return self.rp


def antenna(c, f: Frame, base_x, base_y, p: Pose, colour, bulb_r, glow, out):
    """A stem from (base_x, base_y) in rest coordinates, leaning p.antenna degrees, bulb on top."""
    bx, by = f.pt(base_x, base_y)
    a = math.radians(p.antenna)
    steps = int(p.antenna_len)
    for i in range(steps + 1):
        bend = a * (i / steps) ** 1.3
        c.put(bx + math.sin(bend) * i * 0.95, by - math.cos(bend) * i, colour)
    tx = bx + math.sin(a) * p.antenna_len * 0.95
    ty = by - math.cos(a) * p.antenna_len - bulb_r
    disc = (XS - tx) ** 2 + (YS - ty) ** 2
    c.flat(disc <= (bulb_r + 1) ** 2 + 0.3, out)
    c.flat(disc <= bulb_r ** 2 + 0.3, glow)
    if p.light_level > 0.5:
        c.put(tx - 1, ty - 1, WHITE)
    return tx, ty


WEATHER_COLOUR = {"sunny": "ffd36a", "cloudy": "c4cad6", "rain": "8aa4c8", "snow": "f2f6ff"}


class Boopie(Rig):
    """布比: a round mochi sprite, one antenna leaning right with a glowing bulb."""
    key, name, colour = "boopie", "布比", "ff9ec8"
    look = ""          # tangyuan, jellyfish, slime or weather (a skin's)
    weather = "sunny"  # the weather spirit's, as the day's

    def __init__(self, colour=None, skin=None):
        super().__init__(colour, skin)
        if self.look == "weather":
            glow, out = self.rp["glow"], self.rp["out"]
            self.rp = ramp(WEATHER_COLOUR[self.weather])
            if self.skin and self.skin.glow:
                self.rp["glow"] = glow
            if self.skin and self.skin.outline:
                self.rp["out"] = out
        if self.look == "tangyuan":
            self.mouth_inside = (60, 40, 44)   # 黑芝麻 inside

    def draw(self, c, p):
        cx, cy, rx, ry = 32, 40, 17, 15
        f = self.frame(p, cx, cy + ry)
        body = f.ellipse(cx, cy, rx, ry)
        if self.look == "jellyfish":
            body &= f.ry <= cy + 6               # a bell, its rim straight
        if p.feet and p.scale > 0.6 and self.look not in ("jellyfish", "slime"):
            body |= f.ellipse(cx - 7, cy + ry - 1, 4, 2.5) | f.ellipse(cx + 7, cy + ry - 1, 4, 2.5)
        for h in p.hands:
            if h[0] != "front":
                body |= f.ellipse(cx + h[0] * rx, cy + h[1], 3 / p.squash, 2.6 * p.squash)
        if self.look == "slime":                 # drips along the bottom
            for dx, r in ((-9, 2.2), (1, 2.6), (10, 2.0)):
                body |= f.ellipse(cx + dx, cy + ry - 1, r, 2.4)
        c.shaded(body, self.rp)
        c.outline(body, self.rp["out"])
        self.extras(c, p, f, body, cx, cy, ry)
        light = antenna(c, f, cx + 3, cy - ry + 1, p, self.rp["out"], 2.4, self.light_colour(p), self.rp["out"])
        return {"slots": {"hat": f.pt(27, 27), "eyes": (*f.pt(cx, cy - 1), 7 * f.sx), "neck": (*f.pt(cx, 47), 26 * f.sx)}, "face": f.pt(cx, cy - 1), "body": f.pt(cx, cy), "light": light, "show_face": p.scale > 0.6}

    def extras(self, c, p, f, body, cx, cy, ry):
        t = p.t
        if self.look == "tangyuan":                # steam rising off it
            for i, dx in enumerate((-6, 0, 6)):
                x0, y0 = f.pt(cx + dx, cy - ry - 2)
                for k in range(0, 6, 2):
                    c.put(x0 + round(math.sin(t * 3 + i + k * 0.7)), y0 - k - (i % 2), (226, 226, 236))
        elif self.look == "jellyfish":             # tentacles, swaying, and specks of light inside
            for i, dx in enumerate((-10, -5, 0, 5, 10)):
                x0, y0 = f.pt(cx + dx, cy + 7)
                for k in range(10 - abs(dx) // 3):
                    c.put(x0 + round(math.sin(t * 2.2 + i + k * 0.5) * 1.2), y0 + k,
                          self.rp["light"] if (k + i) % 3 else self.rp["mid"])
            for i in range(5):
                x, y = f.pt(cx - 10 + h01(i, 81) * 20, cy - 8 + h01(i, 82) * 10)
                if math.sin(t * 2 + i * 1.7) > 0:
                    c.put(x, y, self.rp["high"])
        elif self.look == "slime":                 # a wet shine
            x, y = f.pt(cx - 9, cy - 8)
            for dx, dy in ((0, 0), (1, 0), (0, 1), (2, -1)):
                c.put(x + dx, y + dy, WHITE)
        elif self.look == "weather":
            if self.weather == "sunny":            # rays round it, turning
                for k in range(8):
                    a = k * math.pi / 4 + t * 0.6
                    x, y = f.pt(cx + math.cos(a) * 21, cy + math.sin(a) * 19)
                    c.put(x, y, self.rp["glow"])
            elif self.weather == "rain":           # drops falling from it
                for i, dx in enumerate((-8, 0, 8)):
                    k = (t * 9 + i * 3) % 9
                    x, y = f.pt(cx + dx, cy + 15 + k)
                    c.put(x, y, (120, 170, 240))
                    c.put(x, y + 1, (120, 170, 240))
            elif self.weather == "snow":           # flakes settled on top
                for i in range(6):
                    x, y = f.pt(cx - 12 + i * 5, cy - 13 + abs(i - 2.5) * 1.2)
                    c.put(x, y, WHITE)


class Codex(Rig):
    """Codex: a cloud-headed robot whose face is a terminal; its eyes are the prompt, >_ ."""
    key, name, colour = "codex", "Codex", "5b86f5"
    size, squash_k, jump_k = 1.08, 0.35, 0.4
    screen = (30, 34, 84)
    glyph = (120, 236, 240)
    glyph_follows_light = True   # the prompt takes the state light's colour
    wave_face = False            # 凯伦: a green line for a face

    def draw(self, c, p):
        cx, base = 32, 56
        f = self.frame(p, cx, base)
        head = f.ellipse(32, 27, 17, 12)
        for x, y, r in ((22, 18, 6), (30, 15, 7), (38, 15, 6.5), (44, 20, 5.5), (17, 25, 5), (47, 27, 4.5)):
            head |= f.ellipse(x, y, r, r)
        body = f.ellipse(32, 45, 9, 7)
        if p.feet and p.scale > 0.6:
            body |= f.ellipse(28, 52, 2.6, 3.4) | f.ellipse(36, 52, 2.6, 3.4)
        for h in p.hands:
            if h[0] != "front":
                body |= f.ellipse(32 + h[0] * 11, 44 + h[1] * 0.8, 2.8, 2.4)
        whole = head | body
        c.shaded(body, self.rp)
        c.shaded(head, self.rp)
        c.outline(whole, self.rp["out"])
        screen = f.rect(21, 21, 44, 34)
        corners = f.rect(21, 21, 22, 22) | f.rect(43, 21, 44, 22) | f.rect(21, 33, 22, 34) | f.rect(43, 33, 44, 34)
        c.flat(screen & ~corners, self.screen)
        gx, gy = f.pt(32, 45)
        for i, (dx, dy) in enumerate(((-3, -1), (-2, 0), (-3, 1), (0, 0), (1, 0))):   # >- on the chest
            c.put(gx + dx, gy + dy, mix(self.glyph, self.rp["mid"], 0.4))
        face = f.pt(32, 27)
        return {"screen_box": (*f.pt(21, 21), *f.pt(44, 34)), "slots": {"hat": f.pt(31, 10), "eyes": (face[0] - 0.5, face[1], 4.5 * f.sx), "neck": (*f.pt(32, 38), 16 * f.sx)}, "face": face, "body": f.pt(32, 40), "light": f.pt(44, 14), "show_face": p.scale > 0.6}

    def face(self, c, fx, fy, p):
        if self.wave_face:
            g = self.glyph
            amp = 2.5 if p.mouth == "talk" else 1.0 if p.mouth in ("w", "smile") else 0.4
            for x in range(-9, 10):
                y = math.sin((x + p.t * 8) * 0.9) * amp * (1 - abs(x) / 11)
                c.put(fx + x, fy + 1 + y, g)
            if p.eyes not in ("blink", "happy"):
                c.put(fx - 4, fy - 3, g)
                c.put(fx + 4, fy - 3, g)
            return
        col = (p.light if self.glyph_follows_light else None) or self.glyph
        col = mix(self.screen, col, max(0.25, min(1.0, p.light_level)))
        L, R = fx - 5, fx + 4
        k = p.eyes
        if p.mouth == "talk":              # the cursor grows with the voice
            glyph(c, ">", L, fy, col)
            for dy in range(-p.talk, 3):
                for dx in range(3):
                    c.put(R + dx - 1, fy + dy, col)
            return
        pair = {"open": (">", "_"), "look": (">", "_"), "blink": ("-", "-"), "happy": ("^", "^"),
                "wide": ("|", "|"), "worried": (">", "<"), "x": ("x", "x"), "spiral": ("o", "o"),
                "half": ("-", "_"), "down": ("^", "^")}.get(k, (">", "_"))
        if k == "open" and int(p.t * 2) % 2:   # the cursor blinks
            pair = (">", " ")
        lx, ly = p.look if k == "look" else (0, 0)
        glyph(c, pair[0], L + lx, fy + ly, col)
        glyph(c, pair[1], R + lx, fy + ly, col)
        if p.blush == "big":
            for side in (-1, 1):
                for dx in range(-1, 2):
                    c.put(fx + side * 9 + dx, fy + 4, CHEEK)


# The knot of GPT's logo at 16 x 16 (# the bands, . the holes between them),
# traced from the logo once; for personal use only, like the brand itself.
KNOT = [
    "     ####       ",
    "    ##..#####   ",
    "   ##...##..##  ",
    "  ##..##.....## ",
    " #.#.##..###..# ",
    "#..#.#.##..#### ",
    "#..#.##..#...## ",
    "#..#......##..##",
    "##..##......#..#",
    " ##..##..##.#..#",
    " ####..##.#.#..#",
    " #..###..##.#.# ",
    " ##.....##..##  ",
    "  ##..##...##   ",
    "   #####..##    ",
    "       ####     ",
]
KNOT_GRID = np.array([list(r) for r in KNOT])


KNOT_BANDS = np.array([[1.0 if ch == "#" else 0.0 for ch in row] for row in KNOT])
KNOT_ALL = np.array([[1.0 if ch != " " else 0.0 for ch in row] for row in KNOT])


def bilinear(grid, gx, gy):
    """grid sampled at (gx, gy), cell centres at +0.5, 0 outside it."""
    n = grid.shape[0]
    x = np.clip(gx - 0.5, 0, n - 1.001)
    y = np.clip(gy - 0.5, 0, n - 1.001)
    x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
    fx, fy = x - x0, y - y0
    x1, y1 = np.minimum(x0 + 1, n - 1), np.minimum(y0 + 1, n - 1)
    v = (grid[y0, x0] * (1 - fx) * (1 - fy) + grid[y0, x1] * fx * (1 - fy) + grid[y1, x0] * (1 - fx) * fy
         + grid[y1, x1] * fx * fy)
    return np.where((gx >= 0) & (gx < n) & (gy >= 0) & (gy < n), v, 0)


class GPT(Rig):
    """GPT: the knot of its logo, scaled up smooth, as a big head, a round
    little face in its middle, stubby hands and feet."""
    key, name, colour = "gpt", "GPT", "f2f2f2"
    size, squash_k, jump_k = 1.0, 0.6, 0.6
    line = (18, 18, 24)        # outlines
    holes = (64, 64, 74)       # between the bands
    face_colour = (255, 244, 230)
    knot_scale = 2.45
    look = ""            # donut, neon, kaleido or chrome (a skin's)

    def draw(self, c, p):
        cx, cy = 32, 30
        f = self.frame(p, cx, 54)
        gx, gy = (f.rx - cx) / self.knot_scale + 8, (f.ry - cy) / self.knot_scale + 8
        band = bilinear(KNOT_BANDS, gx, gy) > 0.5
        head = bilinear(KNOT_ALL, gx, gy) > 0.5
        face = f.ellipse(cx, cy, 7.5, 7.5)
        limbs = np.zeros_like(head)
        if p.feet and p.scale > 0.6:
            limbs |= f.ellipse(cx - 7, 50.5, 3.8, 2.8) | f.ellipse(cx + 7, 50.5, 3.8, 2.8)
        for h in p.hands:
            if h[0] != "front":
                limbs |= f.ellipse(cx + h[0] * 19.5, cy + 7 + h[1], 3.2, 2.8)
        limbs &= ~head
        c.shaded(limbs, self.rp)
        c.outline(limbs, self.line)
        c.flat(head & ~band & ~face, self.holes)
        bands = band & ~face
        c.shaded(bands, self.rp)
        line = self.line
        if self.look == "neon" and int(p.t * 7) % 23 == 0:
            line = mix(line, (0, 0, 0), 0.5)        # a flicker now and then
        c.outline(bands, line)
        self.extras(c, p, f, bands, cx, cy)
        c.flat(face, self.face_colour)
        c.outline(face, self.line)
        c.body |= head
        return {"slots": {"hat": f.pt(cx, cy - 18), "eyes": (*f.pt(cx, cy - 1), 4 * f.sx),
                          "neck": (*f.pt(cx, cy + 17), 16 * f.sx)},
                "face": f.pt(cx, cy - 1), "body": f.pt(cx, cy), "light": f.pt(cx + 14, cy - 15),
                "show_face": p.scale > 0.6}

    def extras(self, c, p, f, bands, cx, cy):
        t = p.t
        if self.look == "donut":                   # sprinkles on the glaze
            cols = ((255, 255, 255), (255, 220, 80), (110, 200, 255), (140, 230, 120))
            for i in range(22):
                x, y = (round(v) for v in f.pt(cx + (h01(i, 71) - 0.5) * 40, cy + (h01(i, 72) - 0.5) * 40))
                if 0 <= x < N and 0 <= y < N and bands[y, x]:
                    c.put(x, y, cols[i % 4])
        elif self.look == "kaleido":               # the colours going round
            for y, x in zip(*np.nonzero(bands)):
                role = next((k for k, r in enumerate(ROLES) if tuple(c.img[y, x]) == self.rp[r]), 1)
                hue = (math.atan2(y + 0.5 - cy, x + 0.5 - cx) / (2 * math.pi) + t * 0.15) % 1.0
                c.img[y, x] = hue_colour(hue, (0.62, 0.78, 0.9, 1.0)[role])
        elif self.look == "chrome":                # a gleam sweeping across
            for y, x in zip(*np.nonzero(bands)):
                k = (x + y * 0.5 - t * 24) % 70
                if k < 3:
                    c.img[y, x] = WHITE
                elif k < 5:
                    c.img[y, x] = self.rp["high"]

    def face(self, c, fx, fy, p):
        for side in (-1, 1):
            eye(c, fx + side * 3, fy, p, side)
        blush(c, fx, fy - 1, p, 5, self.cheek)
        mouth(c, fx + p.look[0] // 2, fy + 4, p)


GLYPHS = {
    ">": ["#..", ".#.", "..#", ".#.", "#.."], "<": ["..#", ".#.", "#..", ".#.", "..#"],
    "_": ["...", "...", "...", "...", "###"], "-": ["...", "...", "###", "...", "..."],
    "^": ["...", ".#.", "#.#", "...", "..."], "|": [".#.", ".#.", ".#.", ".#.", ".#."],
    "x": ["#.#", "#.#", ".#.", "#.#", "#.#"], "o": ["...", ".#.", "#.#", ".#.", "..."], " ": ["..."] * 5,
}


def glyph(c, ch, x, y, colour):
    for j, row in enumerate(GLYPHS[ch]):
        for i, v in enumerate(row):
            if v == "#":
                c.put(x + i - 1, y + j - 2, colour)


class Klaude(Rig):
    """小克: a blocky orange critter with square eyes, stubby side arms and four little legs."""
    key, name, colour = "klaude", "小克", "f28c5e"
    sponge = False   # 海绵宝宝: holes in the block, little brown shorts
    look = ""        # lantern, mummy or ghost (a skin's)

    def draw(self, c, p):
        cx, base = 32, 51
        f = self.frame(p, cx, base)
        body = f.rect(17, 22, 47, 44)
        if self.look == "ghost":                 # a wavy hem for legs
            hem = 46 + 1.5 * np.sin(f.rx * 0.8 + p.t * 4)
            body = f.rect(17, 22, 47, 50) & (f.ry <= hem)
        for h in p.hands:
            if h[0] != "front":
                up = min(0, h[1]) * 1.6
                x0 = 11 if h[0] < 0 else 47
                body |= f.rect(x0, 31 + up, x0 + 6, 36 + up)
        if p.feet and p.scale > 0.6 and self.look != "ghost":
            for x in (20, 25, 36, 41):
                body |= f.rect(x, 44, x + 3, 51)
        c.shaded(body, self.rp)
        c.outline(body, self.rp["out"])
        torso = f.rect(17, 22, 47, 44)
        if self.look == "lantern":               # ribs, and the stem
            ribs = torso & ((np.abs(f.rx - 24.5) < 0.6) | (np.abs(f.rx - 39.5) < 0.6))
            c.flat(ribs, self.rp["dark"])
            stem = f.rect(30, 18, 34, 22)
            c.flat(stem, (80, 140, 50))
            c.outline(stem, (40, 70, 26))
        elif self.look == "mummy":               # wrapped in bandages, a loose end
            for y, x in zip(*np.nonzero(torso)):
                if (y + x // 6) % 4 == 0:
                    c.img[y, x] = (190, 180, 154)
            x0, y0 = f.pt(47, 34)
            for k in range(5):
                c.put(x0 + 1 + k, y0 + round(math.sin(p.t * 3 + k * 0.8)), (230, 222, 200))
        elif self.look == "ghost":               # see-through, here and there
            for y, x in zip(*np.nonzero(body)):
                if BAYER[y % 4][x % 4] < 0.2:
                    c.img[y, x] = self.rp["dark"]
        if self.sponge:
            torso = f.rect(17, 22, 47, 44)
            for i in range(10):
                hx, hy = 19 + h01(i, 61) * 26, 23 + h01(i, 62) * 13
                if abs(hx - 32) < 11 and 26 < hy < 34:
                    continue                     # not over the face
                hole = f.ellipse(hx, hy, 1.3 + h01(i, 63) * 0.9, 1.1 + h01(i, 64) * 0.7)
                c.flat(hole & torso, (196, 172, 44))
            c.flat(torso & (f.ry >= 40), (150, 96, 44))
            c.flat(torso & (f.ry >= 40) & (f.ry < 41), (90, 56, 24))
        return {"slots": {"hat": f.pt(32, 23), "eyes": (*f.pt(32, 30), 7 * f.sx), "neck": (*f.pt(32, 42), 30 * f.sx)}, "face": f.pt(32, 30), "body": f.pt(32, 35), "light": f.pt(44, 16), "show_face": p.scale > 0.6}

    def face(self, c, fx, fy, p):
        if self.look == "lantern":
            self.lantern_face(c, fx, fy, p)
            return
        eyec = mix(WHITE, p.light, 0.35) if p.light and p.light != WHITE else (255, 246, 236)
        if self.skin and self.skin.eye:
            eyec = self.skin.eye
        c.eye, c.shine = eyec, None
        for side in (-1, 1):
            eye(c, fx + side * 7, fy, p, side, square=True)
        blush(c, fx, fy, p, 11, (255, 176, 150))
        if p.mouth in ("talk", "o", "chomp", "wavy", "frown"):
            mouth(c, fx, fy + 6, p, colour=(92, 34, 22), inside=(170, 60, 50))
        if self.look == "mummy":                  # one eye under the bandages
            for dy in range(-3, 3):
                for dx in range(-10, -4):
                    c.put(fx + dx, fy + dy, (230, 222, 200) if (dy + 3) % 3 else (190, 180, 154))

    def lantern_face(self, c, fx, fy, p):
        """Carved: triangle eyes and a jagged grin, cut dark, a candle flickering inside."""
        cut, glow = (70, 28, 4), ((255, 236, 140) if math.sin(p.t * 9) > -0.3 else (255, 190, 80))
        shut = p.eyes in ("blink", "happy", "down", "half")
        for side in (-1, 1):
            ex = fx + side * 7
            if shut:
                for dx in range(-3, 4):
                    c.put(ex + dx, fy, cut)
                continue
            for dy, w in ((-4, 0), (-3, 1), (-2, 2), (-1, 3), (0, 4), (1, 4)):
                for dx in range(-w, w + 1):
                    c.put(ex + dx, fy + dy, cut)
            for dy, w in ((-2, 0), (-1, 1), (0, 2)):
                for dx in range(-w, w + 1):
                    c.put(ex + dx, fy + dy, glow)
        rows = 3 if p.mouth in ("talk", "o", "chomp") else 2
        for dx in range(-8, 9):
            top = fy + 5 + (1 if dx % 2 else 0)
            for k in range(-1, rows + 1):
                c.put(fx + dx, top + k, cut)
        for dx in range(-7, 8):
            top = fy + 5 + (1 if dx % 2 else 0)
            for k in range(rows):
                c.put(fx + dx, top + k, glow)


class Whale(Rig):
    """DeepSeek 小鲸鱼: a round little whale with a white tummy and a perky tail;
    its spout is the state light."""
    key, name, colour = "whale", "DeepSeek 小鲸鱼", "5a7dff"
    belly = ((214, 224, 255), (240, 244, 255))
    ponytail = False   # 珍珍: a blonde ponytail and a pink bow

    def draw(self, c, p):
        cx, cy, rx, ry = 31, 41, 16, 15
        f = self.frame(p, cx, cy + ry)
        a = math.radians(p.antenna - 18)            # the tail swings like Boopie's antenna
        tx, ty = 46 + 3 * math.sin(a), 21 - 1.5 * math.cos(a)
        tail = f.ellipse(44 + 1.5 * math.sin(a), 27, 2.6, 7.5) | f.ellipse(tx - 3, ty, 3.5, 2) | f.ellipse(tx + 3, ty - 0.5, 3.5, 2)
        body = f.ellipse(cx, cy, rx, ry) | tail
        for h in p.hands:
            if h[0] != "front":
                body |= f.ellipse(cx + h[0] * (rx + 0.5), cy + 6 + h[1] * 0.8, 3.2, 2.0)
        c.shaded(body, self.rp)
        belly = f.ellipse(cx, cy + 8, 10.5, 6) & f.ellipse(cx, cy, rx - 1.2, ry - 1.2)
        c.flat(belly, self.belly[1])
        bay = BAYER[np.arange(N)[:, None] % 4, np.arange(N)[None, :] % 4]
        c.flat(belly & (bay > 0.5) & (YS > f.pt(0, cy + 10)[1]), self.belly[0])
        c.outline(body, self.rp["out"])
        hx, hy = f.pt(cx - 2, cy - ry)
        col = self.light_colour(p)
        h = 2 + round(3 * max(0.0, min(1.0, p.light_level)))
        for i in range(h):
            c.put(hx, hy - 1 - i, col)
        for dx, dy in ((-2, -h), (2, -h), (-3, -h + 2), (3, -h + 2)):
            c.put(hx + dx, hy + dy, col)
        if self.ponytail:
            px, py = f.pt(29, 25)
            for k in range(7):
                for w in (-1, 0, 1):
                    c.put(px - k * 0.6 + w, py - k, (250, 210, 90) if w else (255, 230, 140))
            for dx, dy in ((-2, 0), (-1, 0), (1, 0), (2, 0), (-2, -1), (2, -1), (0, 0)):
                c.put(px + dx, py + 1 + dy, (240, 90, 140))
        return {"slots": {"hat": f.pt(37, 27), "eyes": (*f.pt(cx, cy - 2), 7 * f.sx), "neck": (*f.pt(cx, 47), 24 * f.sx)}, "face": f.pt(cx, cy - 2), "body": f.pt(cx, cy), "light": (hx, hy - h - 1),
                "show_face": p.scale > 0.6}


class Doubao(Rig):
    """豆包: a girl with a brown bob and big eyes, in a black top; her hair clip is the state light."""
    key, name, colour = "doubao", "豆包", "f2c9b4"
    size, squash_k, jump_k = 1.08, 0.35, 0.4
    hair = "6b4a3e"
    top = "3a3a44"
    rem = False   # 蕾姆: a maid's frilly headband, a pink ribbon, her fringe over one eye, a blue eye

    def __init__(self, colour=None, skin=None):
        super().__init__(colour, skin)
        self.hp = ramp(self.hair)
        self.tp = ramp(self.top)

    def draw(self, c, p):
        cx, base = 32, 57
        f = self.frame(p, cx, base)
        torso = f.ellipse(32, 49, 10, 7) & f.rect(0, 43, 64, 57)
        legs = (f.rect(27, 54, 31, 57) | f.rect(33, 54, 37, 57)) if p.feet and p.scale > 0.6 else np.zeros_like(torso)
        arms = np.zeros_like(torso)
        for h in p.hands:
            if h[0] != "front":
                arms |= f.ellipse(32 + h[0] * 11, 48 + h[1] * 0.8, 2.6, 2.4)
        hair = f.ellipse(32, 26, 18, 16) & f.rect(0, 0, 64, 40)
        hair |= f.rect(14, 22, 21, 40) | f.rect(43, 22, 50, 40)
        face = f.ellipse(32, 30, 13, 11)
        bangs = f.ellipse(36, 17, 14, 7) | f.ellipse(22, 18, 7, 6)
        skin = (face & ~bangs) | arms
        whole = hair | torso | legs | arms | face
        c.shaded(torso | legs, self.tp)
        c.shaded(hair, self.hp)
        c.shaded(skin, self.rp)
        c.outline(whole, self.hp["out"])
        bx, by = f.pt(32, 48)
        c.put(bx, by, (230, 230, 236))
        clip = f.ellipse(45, 16, 2.4, 1.6)
        c.flat(clip, self.light_colour(p))
        c.outline(clip, self.hp["out"])
        light = f.pt(45, 16)
        if self.rem:
            for x in range(19, 46, 4):           # the headband's frills
                px, py = f.pt(x, 13 - 3.2 * math.cos((x - 32) / 13 * 1.2))
                for dx, dy in ((0, 0), (1, 0), (0, -1), (1, -1), (-1, 0)):
                    c.put(px + dx, py + dy, (252, 252, 252))
                c.put(px, py + 1, (40, 40, 48))
            light = f.pt(46, 20)                 # the ribbon and its X clip
            rx, ry = light
            for dx, dy in ((-1, -1), (1, 1), (1, -1), (-1, 1), (0, 0)):
                c.put(rx + dx, ry + dy, (230, 70, 150))
            for k in range(2, 8):
                c.put(rx + (k % 2), ry + k, (230, 70, 150))
            qx, qy = f.pt(32, 44)                # the frilled collar
            for dx in range(-4, 5):
                c.put(qx + dx, qy, (250, 250, 250))
            c.put(qx, qy + 1, (30, 30, 40))
        return {"slots": {"hat": f.pt(29, 11), "eyes": (*f.pt(32, 30), 6 * f.sx), "neck": (*f.pt(32, 43), 12 * f.sx)}, "face": f.pt(32, 30), "body": f.pt(32, 44), "light": light, "show_face": p.scale > 0.6}

    # 蕾姆's fringe: its right edge, a row at a time from 8 above the eyes to 3 below.
    FRINGE = (-1, -1, -2, -2, -3, -3, -3, -4, -4, -5, -5, -5)

    def face(self, c, fx, fy, p):
        self.face_doubao(c, fx, fy, p)
        if not self.rem:
            return
        if p.eyes in ("open", "look", "wide", "worried"):
            lx, ly = p.look if p.eyes == "look" else (0, 0)
            c.put(fx + 6 + lx, fy + ly, (70, 120, 220))
            c.put(fx + 6 + lx, fy + 1 + ly, (70, 120, 220))
        for j, edge in enumerate(self.FRINGE):
            y = j - 8
            for x in range(-11, edge + 1):
                c.put(fx + x, fy + y, self.hp["dark"] if (x - y) % 4 == 0 else self.hp["mid"])
            c.put(fx + edge + 1, fy + y, self.hp["out"])
        for x in range(-11, -5):
            c.put(fx + x, fy + 4, self.hp["out"])

    def face_doubao(self, c, fx, fy, p):
        c.eye = (58, 36, 30)
        for side in (-1, 1):
            eye(c, fx + side * 6, fy, p, side)
            if p.eyes in ("open", "look", "worried", "wide"):   # lashes
                c.put(fx + side * 6 + side * 2, fy - 2 + p.look[1], c.eye)
        blush(c, fx, fy, p, 9, (250, 150, 150))
        mouth(c, fx, fy + 5, p, colour=(190, 90, 80), inside=(235, 120, 120))

    def hand_ramp(self):
        return self.rp


# ---------------------------------------------------------------- accessories
# Worn in the slots each character marks ("slots" in draw()'s anchors): hat
# (bottom centre), eyes (centre, half the gap between them; none worn there
# yet), neck (centre, width). One item a slot.
HAT_SPRITES = {
    "bow": (["##.....##", "#pp#.#pp#", "#ppp#ppp#", "#pp#.#pp#", "##.....##"],
            {"#": (150, 40, 80), "p": (255, 120, 170)}),
    "party_hat": (["...w...", "..www..", "...#...", "..#p#..", "..#y#..", ".#ppp#.", ".#yyy#.", "#ppppp#", "#######"],
                  {"w": (255, 255, 255), "#": (60, 40, 110), "p": (120, 200, 255), "y": (255, 220, 90)}),
    "crown": (["g...g...g", "gg.ggg.gg", "ggggggggg", "grgggggbg", "ddddddddd"],
              {"g": (255, 210, 70), "d": (190, 130, 30), "r": (240, 60, 80), "b": (80, 170, 255)}),
    "straw_hat": (["....yyyyy....", "...yyyyyyy...", "...rrrrrrr...", "ydyyyyyyyyydy", ".yyyyyyyyyyy."],
                  {"y": (240, 205, 110), "d": (196, 160, 70), "r": (220, 70, 70)}),
    "halo": (["..yyyyy..", ".y.....y.", "..yyyyy..", ".........", "........."],
             {"y": (255, 226, 110)}),
}
ACCESSORIES = {   # name: slot, Chinese name
    "bow": ("hat", "蝴蝶结"), "party_hat": ("hat", "生日帽"), "crown": ("hat", "小皇冠"),
    "scarf": ("neck", "红围巾"),
    # Earned, not unlocked by level: 收获 50 次, 连续 7 天来看它, 破纪录 10 次.
    "straw_hat": ("hat", "草帽"), "halo": ("hat", "金光环"), "medal": ("neck", "金牌"),
}


def sprite(c: Canvas, rows, colours, x, y):
    for j, row in enumerate(rows):
        for i, ch in enumerate(row):
            if ch in colours:
                c.put(x + i, y + j, colours[ch])


def wear(c: Canvas, name: str, slots: dict):
    slot = ACCESSORIES[name][0]
    if slot == "hat":
        rows, colours = HAT_SPRITES[name]
        hx, hy = slots["hat"]
        sprite(c, rows, colours, round(hx - len(rows[0]) / 2), round(hy) - len(rows) + 1)
    elif name == "scarf":
        nx, ny, w = slots["neck"]
        red, dark = (230, 60, 70), (160, 30, 45)
        for dx in range(-round(w / 2), round(w / 2) + 1):
            for dy in range(0, 3):
                c.put(nx + dx, ny + dy, dark if (dx + dy) % 4 == 0 else red)
        for dy in range(3, 8):   # the tail
            for dx in range(2):
                c.put(nx + w / 4 + dx + (dy > 5), ny + dy, dark if dy == 7 else red)
    elif name == "medal":
        nx, ny, w = slots["neck"]
        for k in range(4):       # the ribbon, in a V
            c.put(nx - 3 + k, ny + k * 0.6, (70, 120, 220))
            c.put(nx + 3 - k, ny + k * 0.6, (220, 60, 70))
        for dy in range(3, 7):
            for dx in range(-2, 3):
                if abs(dx) + abs(dy - 4.5) <= 2.6:
                    c.put(nx + dx, ny + dy, (255, 210, 70) if (dx + dy) % 3 else (200, 150, 40))


# ---------------------------------------------------------------- scenes
# The idle background a skin brings: drawn behind the character, in front of
# it, or over the whole frame afterwards (glitch). "default" is Muse's own:
# the glow and sparkles alone.
SCENES = {
    "default": "默认光晕", "stars": "星空", "fireflies": "萤火", "snow": "飘雪",
    "petals": "花瓣", "bubbles": "气泡", "matrix": "代码雨", "neon_grid": "霓虹网格", "glitch": "像素故障",
}


def h01(*v) -> float:
    """A stable hash of the arguments, 0..1."""
    x = 0x9E3779B9
    for k in v:
        x = (x ^ (int(k) * 0x85EBCA6B + 0x632BE5AB)) & 0xFFFFFFFF
        x = (x * 0x27D4EB2D) & 0xFFFFFFFF
        x ^= x >> 15
    return (x & 0xFFFFFF) / 16777216.0


def scene_back(c: Canvas, scene: str, t: float, acc):
    if scene == "stars":
        for i in range(34):
            x, y = int(h01(i, 1) * 64), int(h01(i, 2) * 50)
            tw = 0.5 + 0.5 * math.sin(t * (1.5 + h01(i, 3) * 2) + i)
            if tw > 0.35:
                c.put(x, y, mix((40, 40, 70), (230, 230, 255), tw))
            if tw > 0.9 and i % 5 == 0:
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    c.put(x + dx, y + dy, (90, 90, 140))
        k = (t % 4.0) / 0.6                       # a shooting star every 4 s
        if k < 1:
            for j in range(6):
                c.put(50 - k * 30 + j, 4 + k * 12 - j * 0.4, mix((255, 255, 255), (60, 60, 110), j / 6))
    elif scene == "fireflies":
        for i in range(9):
            if i % 3 == 0:
                continue                           # those fly in front
            firefly(c, i, t)
    elif scene == "snow":
        for i in range(22):
            snowflake(c, i, t, False)
    elif scene == "petals":
        for i in range(12):
            petal(c, i, t, False)
    elif scene == "bubbles":
        for i in range(9):
            sp, r = 6 + h01(i, 2) * 6, 1 + int(h01(i, 4) * 2)
            y = 66 - (h01(i, 3) * 70 + t * sp) % 72
            x = h01(i, 1) * 64 + math.sin(t * 2 + i) * 1.5
            ring = [(dx, dy) for dx in range(-r, r + 1) for dy in range(-r, r + 1)
                    if r - 0.5 <= math.hypot(dx, dy) <= r + 0.5]
            for dx, dy in ring:
                c.put(x + dx, y + dy, (90, 160, 210))
            c.put(x - r / 2, y - r / 2, (220, 240, 255))
    elif scene == "matrix":
        for i in range(16):
            x = i * 4 + 1
            sp, ln = 14 + h01(i, 2) * 16, 6 + int(h01(i, 3) * 8)
            head = (h01(i, 1) * 90 + t * sp) % 90 - 10
            for j in range(ln):
                y = int(head) - j
                if 0 <= y < 64 and h01(i, y, int(t * 6) if j == 0 else 0) > 0.25:
                    col = (200, 255, 210) if j == 0 else mix((30, 200, 90), (6, 40, 18), j / ln)
                    c.put(x, y, col)
    elif scene == "neon_grid":
        horizon = 44
        for x in range(64):                        # a sunset band
            for y in range(horizon - 10, horizon):
                if BAYER[y % 4][x % 4] < (y - horizon + 10) / 12:
                    c.put(x, y, (90, 30, 90))
        for k in range(7):                         # rows rushing toward you
            d = ((k + t * 0.8) % 7) / 7
            y = horizon + d * d * 20
            for x in range(64):
                c.put(x, y, (200, 60, 200) if d > 0.4 else (110, 40, 130))
        for k in range(-8, 9):                     # rails to the vanishing point
            for y in range(horizon, 64):
                x = 32 + k * 2.2 * (y - horizon) / 6 + k * 0.6
                c.put(x, y, (130, 50, 160))


def scene_front(c: Canvas, scene: str, t: float):
    if scene == "fireflies":
        for i in range(0, 9, 3):
            firefly(c, i, t)
    elif scene == "snow":
        for i in range(22, 30):
            snowflake(c, i, t, True)
    elif scene == "petals":
        for i in range(12, 16):
            petal(c, i, t, True)


def scene_post(img, scene: str, t: float):
    if scene != "glitch":
        return img
    out = img.copy()
    out[1::2] = (out[1::2] * 0.82).astype(np.uint8)     # scanlines
    burst = int(t / 1.6)
    if (t % 1.6) < 0.35:                                # a burst every 1.6 s
        for b in range(3):
            y0 = int(h01(burst, b, 1) * 56)
            hgt = 2 + int(h01(burst, b, 2) * 6)
            shift = int((h01(burst, b, 3) - 0.5) * 10)
            out[y0:y0 + hgt] = np.roll(out[y0:y0 + hgt], shift, axis=1)
        out[:, :, 0] = np.roll(out[:, :, 0], 1, axis=1)  # red split
        for k in range(10):
            x, y = int(h01(burst, k, 4) * 64), int(h01(burst, k, 5) * 64)
            out[y, x:x + 3] = (80, 255, 200) if k % 2 else (255, 60, 160)
    return out


def firefly(c, i, t):
    x = 32 + 26 * math.sin(t * 0.4 * (1 + h01(i, 1)) + i * 2.1)
    y = 30 + 20 * math.sin(t * 0.33 * (1 + h01(i, 2)) + i * 1.3)
    glow = 0.5 + 0.5 * math.sin(t * 3 + i * 1.7)
    core = mix((80, 90, 30), (230, 255, 140), glow)
    c.put(x, y, core)
    if glow > 0.5:
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            c.put(x + dx, y + dy, mix((20, 24, 10), (110, 130, 50), glow))


def snowflake(c, i, t, front):
    sp = 5 + h01(i, 2) * 6 + (4 if front else 0)
    y = (h01(i, 3) * 64 + t * sp) % 68 - 2
    x = h01(i, 1) * 64 + math.sin(t * 1.3 + i) * 2
    col = (250, 250, 255) if front else (150, 160, 200)
    c.put(x, y, col)
    if front:
        c.put(x + 1, y, (200, 210, 240))


def petal(c, i, t, front):
    sp = 7 + h01(i, 2) * 6
    y = (h01(i, 3) * 64 + t * sp) % 68 - 2
    x = (h01(i, 1) * 70 + t * 5 + math.sin(t * 1.8 + i) * 3) % 70 - 3
    a = int(t * 3 + i) % 2
    col, dark = ((255, 190, 215), (230, 130, 170)) if front else ((170, 110, 140), (120, 70, 100))
    c.put(x, y, col)
    c.put(x + 1, y + a, dark)


# ---------------------------------------------------------------- skins
# A skin restyles one character: its colours (body, eyes, cheeks, state light,
# and the rig's own, like Codex's screen), patterns over the body and over the
# face, and the background it brings. Bought with stars; see
# docs/boopie-character.md.
@dataclass
class Skin:
    key: str
    rig: str            # the character it's for (Rig.key)
    name: str
    rarity: str         # "普通" or "典藏"
    price: int          # stars
    scene: str          # its background (SCENES)
    colour: str         # body colour
    eye: tuple | None = None
    cheek: tuple | None = None
    glow: tuple | None = None
    outline: tuple | None = None
    extra: dict = field(default_factory=dict)   # rig attributes it sets
    body_fx: str = ""   # drawn over the body: "starry", "jelly", "ink", ...
    face_fx: str = ""   # drawn over the face: "scanlines"
    back_fx: str = ""   # drawn behind the character, over the background: "embers"
    draft: bool = False # still a design under review, not in the firmware
    limited: str = ""   # on sale only in a festival ("spring", "halloween", "xmas"); kept once bought


SKINS = {
    "boopie_starry": Skin("boopie_starry", "boopie", "星空", "典藏", 300, "stars", "1f1856",
                          eye=(236, 236, 255), cheek=(214, 120, 210), glow=(255, 236, 160),
                          outline=(140, 120, 236), body_fx="starry"),
    # Drafts, two a character, for review.
    "boopie_jelly": Skin("boopie_jelly", "boopie", "果冻", "普通", 150, "bubbles", "7fe6d4",
                         cheek=(255, 140, 170), outline=(40, 140, 130), body_fx="jelly"),
    "muse_astronaut": Skin("muse_astronaut", "muse", "宇航员", "典藏", 300, "stars", "e8ecf2",
                           extra={"visor": "1d2b4a"}),
    "muse_matcha": Skin("muse_matcha", "muse", "抹茶", "普通", 150, "fireflies", "8fbf6a"),
    "gpt_black": Skin("gpt_black", "gpt", "黑结", "典藏", 300, "default", "26262e", outline=(150, 150, 165),
                      extra={"line": (150, 150, 165), "holes": (250, 250, 250)}),
    "gpt_paper": Skin("gpt_paper", "gpt", "白纸", "普通", 150, "default", "f2f2f2",
                      extra={"holes": (255, 255, 255)}),
    "codex_neon": Skin("codex_neon", "codex", "霓虹", "典藏", 300, "neon_grid", "120e24",
                       cheek=(255, 60, 200), outline=(0, 240, 255), body_fx="neon",
                       extra={"screen": (10, 0, 20), "glyph": (255, 70, 210), "glyph_follows_light": False},
                       ),
    "codex_glitch": Skin("codex_glitch", "codex", "赛博故障", "普通", 150, "glitch", "5a3cf0",
                         cheek=(0, 255, 200), outline=(20, 10, 60),
                         extra={"screen": (6, 6, 20), "glyph": (0, 255, 200), "glyph_follows_light": False},
                         body_fx="cyber"),
    "klaude_ice": Skin("klaude_ice", "klaude", "冰块", "典藏", 300, "snow", "a9dcf2",
                       eye=(40, 90, 140), cheek=(150, 200, 240), outline=(70, 140, 190), body_fx="ice"),
    "klaude_lava": Skin("klaude_lava", "klaude", "熔岩", "普通", 150, "default", "1c1414",
                        eye=(255, 236, 120), cheek=(255, 80, 40), outline=(190, 30, 20), body_fx="lava",
                        back_fx="embers"),
    "whale_koi": Skin("whale_koi", "whale", "锦鲤", "典藏", 300, "bubbles", "f6f2ec",
                      outline=(150, 70, 50), extra={"belly": ((236, 230, 222), (250, 248, 244))}, body_fx="koi",
                      ),
    "whale_deepsea": Skin("whale_deepsea", "whale", "深海", "普通", 150, "fireflies", "16285e",
                          eye=(220, 255, 255), outline=(60, 180, 220), glow=(80, 240, 255),
                          extra={"belly": ((30, 52, 104), (40, 72, 134))}, body_fx="glow_spots"),
    "doubao_sakura": Skin("doubao_sakura", "doubao", "樱花", "普通", 150, "petals", "f2c9b4",
                          glow=(255, 160, 200), extra={"hair": "d9809e", "top": "f6c6d6"}),
    "doubao_winter": Skin("doubao_winter", "doubao", "冬装", "典藏", 300, "snow", "f2c9b4",
                          extra={"top": "e6d9bf"}, body_fx="winter"),
    # A third for each, a new palette (★200), and three for festivals only (★250).
    "muse_patrick": Skin("muse_patrick", "muse", "派大星", "普通", 200, "bubbles", "ffbccd"),
    "muse_berry": Skin("muse_berry", "muse", "莓果", "普通", 200, "petals", "b46ab4"),
    "gpt_green": Skin("gpt_green", "gpt", "经典绿", "普通", 200, "default", "10a37f", outline=(6, 40, 30),
                      extra={"line": (6, 40, 30), "holes": (230, 255, 245)}),
    "codex_retro": Skin("codex_retro", "codex", "复古绿屏", "普通", 200, "matrix", "d8d2bc",
                        cheek=(230, 150, 120), outline=(110, 100, 80),
                        extra={"screen": (8, 30, 10), "glyph": (80, 255, 120), "glyph_follows_light": False}),
    "klaude_sponge": Skin("klaude_sponge", "klaude", "海绵宝宝", "普通", 200, "bubbles", "f7e14d", eye=(70, 46, 20),
                          extra={"sponge": True}),
    "whale_sunset": Skin("whale_sunset", "whale", "晚霞", "普通", 200, "default", "ff9e7a",
                         outline=(180, 80, 60), extra={"belly": ((255, 214, 176), (255, 240, 220))}),
    "doubao_sport": Skin("doubao_sport", "doubao", "运动装", "普通", 200, "default", "f2c9b4",
                         extra={"hair": "3a2a20", "top": "3a7bd5"}),
    "boopie_newyear": Skin("boopie_newyear", "boopie", "新春", "典藏", 200, "default", "cc2a2a",
                           cheek=(255, 176, 160), glow=(255, 211, 74), outline=(138, 16, 16), limited="spring"),
    "klaude_lantern": Skin("klaude_lantern", "klaude", "南瓜灯", "典藏", 200, "default", "ff8c1a",
                           outline=(150, 60, 0), extra={"look": "lantern"}, limited="halloween"),
    "whale_xmas": Skin("whale_xmas", "whale", "圣诞树", "典藏", 200, "snow", "2f8f4f",
                       glow=(255, 211, 74), outline=(20, 90, 40),
                       extra={"belly": ((216, 240, 216), (244, 255, 244))}, limited="xmas"),
    # Muse in armour: silver plates from the neck down, a pink bow, a sword (drawn over Muse's own frame).
    "muse_knight": Skin("muse_knight", "muse", "骑士", "典藏", 300, "default", "d9c7a8"),
    # Muse in animal hoods and a wizard's robe (drawn over Muse's own frame: boopie_pixel.c muse_headgear).
    "muse_bear": Skin("muse_bear", "muse", "小熊", "普通", 200, "default", "a9784e"),
    "muse_bunny": Skin("muse_bunny", "muse", "小兔", "普通", 200, "petals", "f6f2f4"),
    "muse_cat": Skin("muse_cat", "muse", "猫咪", "普通", 200, "default", "b4b8c4"),
    "muse_frog": Skin("muse_frog", "muse", "青蛙", "普通", 200, "fireflies", "8cc864"),
    "muse_dino": Skin("muse_dino", "muse", "恐龙", "普通", 250, "default", "5fb08a"),
    "muse_wizard": Skin("muse_wizard", "muse", "巫师", "典藏", 300, "stars", "6a4fb8"),
    # Themes: 蕾姆, and two of 海绵宝宝's friends.
    "doubao_rem": Skin("doubao_rem", "doubao", "蕾姆", "典藏", 300, "petals", "f2c9b4",
                       extra={"hair": "8cc0f0", "top": "f3a6c4", "rem": True}),
    "whale_pearl": Skin("whale_pearl", "whale", "珍珍", "普通", 200, "bubbles", "aebdd2",
                        extra={"belly": ((226, 232, 240), (244, 247, 252)), "ponytail": True}),
    # Round two: Halloween's 小克, 布比's sweets and creatures, GPT's knot as treats and lights.
    "klaude_mummy": Skin("klaude_mummy", "klaude", "木乃伊", "普通", 250, "default", "e9e2d0", eye=(40, 30, 30),
                         outline=(120, 108, 84), extra={"look": "mummy"}),
    "klaude_ghost": Skin("klaude_ghost", "klaude", "小幽灵", "普通", 250, "fireflies", "eef2ff", eye=(30, 30, 44),
                         outline=(120, 130, 170), extra={"look": "ghost"}),
    "boopie_tangyuan": Skin("boopie_tangyuan", "boopie", "汤圆", "典藏", 200, "default", "fbf8f2",
                            outline=(170, 160, 150), extra={"look": "tangyuan"}, limited="spring,lantern"),
    "boopie_jellyfish": Skin("boopie_jellyfish", "boopie", "水母", "典藏", 300, "bubbles", "c4b4ff",
                             glow=(150, 240, 255), extra={"look": "jellyfish"}),
    "boopie_slime": Skin("boopie_slime", "boopie", "史莱姆", "成就", 0, "default", "7cc8ff",
                         outline=(40, 100, 170), extra={"look": "slime"}),
    "boopie_weather": Skin("boopie_weather", "boopie", "天气精灵", "普通", 250, "default", "ffd36a",
                           extra={"look": "weather"}),
    "gpt_donut": Skin("gpt_donut", "gpt", "甜甜圈", "普通", 200, "default", "ff9ec8", outline=(120, 70, 40),
                      extra={"line": (120, 70, 40), "holes": (232, 184, 120), "look": "donut"}),
    "gpt_neon": Skin("gpt_neon", "gpt", "霓虹灯管", "普通", 250, "neon_grid", "1c1030", outline=(0, 240, 255),
                     extra={"line": (0, 240, 255), "holes": (10, 6, 20), "look": "neon"}),
    "gpt_kaleido": Skin("gpt_kaleido", "gpt", "万花筒", "普通", 250, "default", "f2f2f2",
                        extra={"holes": (40, 40, 52), "look": "kaleido"}),
    "gpt_chrome": Skin("gpt_chrome", "gpt", "铬金属", "典藏", 300, "default", "c8ccd4", outline=(70, 74, 84),
                       extra={"line": (70, 74, 84), "holes": (40, 42, 50), "look": "chrome"}),
    "codex_karen": Skin("codex_karen", "codex", "凯伦", "普通", 200, "default", "b4b9c4",
                        extra={"screen": (8, 14, 10), "glyph": (90, 240, 120), "glyph_follows_light": False,
                               "wave_face": True}),
}


def in_body(c: Canvas, x, y) -> bool:
    return 0 <= x < N and 0 <= y < N and bool(c.body[y, x])


def skin_body(c: Canvas, skin: Skin, pose: Pose, anchors: dict):
    bx, by = anchors["body"]
    t = pose.t
    fx = skin.body_fx
    if fx == "jelly":       # a glossy highlight and bubbles rising inside
        for y in range(N):
            for x in range(N):
                if in_body(c, x, y) and ((x - bx + 8) / 6) ** 2 + ((y - by + 8) / 3.5) ** 2 <= 1 and BAYER[y % 4][x % 4] < 0.7:
                    c.put(x, y, (225, 255, 250))
        for i in range(6):
            k = (t * (0.15 + h01(i, 3) * 0.2) + h01(i, 1)) % 1
            x, y = round(bx + (h01(i, 2) - 0.5) * 22), round(by + 10 - k * 22)
            if in_body(c, x, y):
                c.put(x, y, (200, 255, 245))
    elif fx == "ink":       # ink pooling in the lower body, a few drops
        for y in range(N):
            for x in range(N):
                if not in_body(c, x, y):
                    continue
                d = (y - by) / 14 + (h01(x // 3, y // 3, 5) - 0.5) * 0.5
                if d > 0.2 and BAYER[y % 4][x % 4] < d:
                    c.put(x, y, mix((150, 150, 150), (40, 40, 44), min(1, d)))
        for i in range(4):
            x, y = round(bx + (h01(i, 6) - 0.5) * 24), round(by + (h01(i, 7) - 0.8) * 16)
            if in_body(c, x, y):
                c.put(x, y, (30, 30, 34))
    elif fx == "porcelain":  # a blue band and little blue flowers
        for y in range(N):
            for x in range(N):
                if in_body(c, x, y) and 7 <= y - by <= 9 + (x % 4 == 0):
                    c.put(x, y, (50, 80, 180))
        for i in range(5):
            x, y = round(bx + (h01(i, 8) - 0.5) * 22), round(by + (h01(i, 9) - 0.9) * 12)
            for dx, dy in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1)):
                if in_body(c, x + dx, y + dy):
                    c.put(x + dx, y + dy, (70, 100, 200) if (dx or dy) else (220, 230, 250))
    elif fx == "cyber":     # cyan lines across the body, magenta specks
        for y in range(N):
            for x in range(N):
                if in_body(c, x, y):
                    if (y + int(t * 6)) % 4 == 0:
                        c.put(x, y, (0, 230, 210))
                    elif h01(x, y, int(t * 4)) > 0.97:
                        c.put(x, y, (255, 40, 180))
    elif fx == "neon":      # a magenta tube inside the cyan one, and a glow outside
        for y in range(N):
            for x in range(N):
                if in_body(c, x, y):
                    inner = all(in_body(c, x + dx, y + dy) for dx, dy in ((2, 0), (-2, 0), (0, 2), (0, -2)))
                    edge1 = all(in_body(c, x + dx, y + dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
                    if edge1 and not inner:
                        c.put(x, y, (255, 60, 200))
                else:
                    near = any(in_body(c, x + dx, y + dy) for dx in (-3, -2, 2, 3) for dy in (-3, -2, 0, 2, 3))
                    if near and not any(in_body(c, x + dx, y + dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))) \
                            and BAYER[y % 4][x % 4] < 0.35:
                        c.put(x, y, (0, 90, 110))
    elif fx == "ice":       # a shine across it, cracks, frost at the edges
        for y in range(N):
            for x in range(N):
                if not in_body(c, x, y):
                    continue
                d = (x - bx) + (y - by)
                if -14 <= d <= -11 and BAYER[y % 4][x % 4] < 0.8:
                    c.put(x, y, (240, 252, 255))
                edge = not (in_body(c, x - 2, y) and in_body(c, x + 2, y) and in_body(c, x, y - 2) and in_body(c, x, y + 2))
                if edge and BAYER[y % 4][x % 4] < 0.5:
                    c.put(x, y, (225, 245, 255))
        x, y = round(bx + 6), round(by - 6)
        for k in range(7):                      # a crack
            x += 1 if k % 2 else 0
            y += 1
            if in_body(c, x, y):
                c.put(x, y, (250, 255, 255))
    elif fx == "lava":      # cooled black plates, molten red seams between them, pulsing
        fx0, fy0 = anchors["face"]
        seeds = [(bx + (h01(i, 30) - 0.5) * 30, by + (h01(i, 31) - 0.5) * 22) for i in range(7)]
        for y in range(N):
            for x in range(N):
                if not in_body(c, x, y) or (abs(x - fx0) < 11 and abs(y - fy0) < 4):
                    continue   # the rock stays dark round the eyes, so they glow
                d = sorted(math.hypot(x - sx, y - sy) for sx, sy in seeds)
                gap = d[1] - d[0]
                heat = 0.75 + 0.25 * math.sin(t * 2.5 + (x + y) * 0.3)
                if gap < 0.7:
                    c.put(x, y, mix((210, 30, 10), (255, 210, 60), heat))
                elif gap < 1.6 and BAYER[y % 4][x % 4] < 0.55:
                    c.put(x, y, mix((90, 10, 5), (190, 35, 12), heat))
    elif fx == "koi":       # red-orange patches
        for i in range(4):
            px, py = bx + (h01(i, 12) - 0.5) * 24, by + (h01(i, 13) - 0.7) * 16
            rx, ry = 4 + h01(i, 14) * 4, 3 + h01(i, 15) * 3
            for y in range(N):
                for x in range(N):
                    if in_body(c, x, y) and ((x - px) / rx) ** 2 + ((y - py) / ry) ** 2 <= 1:
                        c.put(x, y, (238, 92, 50) if BAYER[y % 4][x % 4] > 0.2 else (250, 140, 90))
    elif fx == "glow_spots":  # glowing spots, pulsing
        for i in range(9):
            x, y = round(bx + (h01(i, 16) - 0.5) * 28), round(by + (h01(i, 17) - 0.7) * 18)
            g = 0.5 + 0.5 * math.sin(t * 2 + i)
            if in_body(c, x, y):
                c.put(x, y, mix((40, 90, 140), (120, 255, 255), g))
    elif fx == "winter":    # a cable-knit jumper and a red scarf
        nx, ny, _ = anchors["slots"]["neck"]
        knit = {tuple(v) for v in ramp("e6d9bf").values()}
        for y in range(round(ny) + 2, N):
            for x in range(N):
                if in_body(c, x, y) and tuple(c.img[y, x]) in knit and (x % 3 == 0 or (x % 3 == 1 and y % 2)):
                    c.put(x, y, (200, 186, 158))
        red, dark = (230, 50, 60), (165, 25, 40)
        for dy in range(-2, 2):                                # a thick red scarf
            for dx in range(-10, 11):
                c.put(nx + dx, ny + dy, dark if (dx + dy) % 4 == 0 else red)
        for dy in range(2, 9):                                 # its end, hanging
            for dx in (4, 5, 6):
                c.put(nx + dx + (dy > 6), ny + dy, dark if dy == 8 or dx == 6 else red)
    if fx == "starry":      # a galaxy: a milky band, stars of every size, a constellation, a crescent moon
        for y in range(N):
            for x in range(N):
                if not in_body(c, x, y):
                    continue
                k = (y - by) / 10                               # deeper at the top
                if k < -0.4 and BAYER[y % 4][x % 4] < -k - 0.3:
                    c.put(x, y, (18, 14, 52))
                band = abs((x - bx) * 0.55 + (y - by) * 0.9 - 2)  # the milky way, across
                if band < 4.5:
                    w = 1 - band / 4.5
                    if BAYER[y % 4][x % 4] < w * 0.9:
                        c.put(x, y, (150, 90, 210) if h01(x, y, 40) > 0.5 else (90, 120, 230))
                    if w > 0.5 and h01(x, y, 41) > 0.9:
                        c.put(x, y, (240, 230, 255))
        for i in range(16):                                    # stars
            x, y = round(bx + (h01(i, 7) - 0.5) * 30), round(by + (h01(i, 8) - 0.5) * 24)
            if not in_body(c, x, y):
                continue
            tw = 0.5 + 0.5 * math.sin(t * (1 + h01(i, 9) * 2) + i * 1.3)
            col = [(255, 250, 230), (200, 230, 255), (255, 220, 240)][i % 3]
            c.put(x, y, mix((70, 60, 140), col, tw))
            if i < 3 and tw > 0.4:                             # three bright ones, four-pointed
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    if in_body(c, x + dx, y + dy):
                        c.put(x + dx, y + dy, mix((90, 80, 170), col, tw * 0.8))
        stars = [(bx - 9, by - 3), (bx - 5, by - 6), (bx + 1, by - 5), (bx + 5, by - 8)]   # a little dipper
        for (x0, y0), (x1, y1) in zip(stars, stars[1:]):
            for k in range(1, 8):
                x, y = round(x0 + (x1 - x0) * k / 8), round(y0 + (y1 - y0) * k / 8)
                if in_body(c, x, y) and k % 2:
                    c.put(x, y, (110, 100, 190))
        for x, y in stars:
            if in_body(c, round(x), round(y)):
                c.put(x, y, (255, 255, 255))
        lx, ly = anchors["light"]                              # the bulb is a crescent moon
        for dy in range(-3, 4):
            for dx in range(-3, 4):
                if (dx - 1.2) ** 2 + (dy + 1.0) ** 2 <= 4.2 and dx * dx + dy * dy <= 6.5:
                    c.put(lx + dx, ly + dy, (12, 10, 30))


def skin_back(c: Canvas, skin: Skin, t: float):
    if skin.back_fx == "embers":   # sparks rising off the lava
        for i in range(14):
            k = (t * (0.25 + h01(i, 50) * 0.3) + h01(i, 51)) % 1
            x = h01(i, 52) * 64 + math.sin(t * 2 + i) * 2
            y = 62 - k * 62
            col = mix((255, 200, 60), (120, 20, 10), k)
            c.put(x, y, col)
            if k < 0.3:
                c.put(x, y + 1, mix((200, 60, 20), (80, 10, 5), k * 3))


def skin_face(c: Canvas, skin: Skin, pose: Pose, anchors: dict):
    if skin.face_fx == "scanlines" and "screen_box" in anchors:   # an old CRT's
        x0, y0, x1, y1 = (round(v) for v in anchors["screen_box"])
        for y in range(max(0, y0), min(N, y1)):
            if y % 2:
                c.img[y, max(0, x0):min(N, x1)] = (c.img[y, max(0, x0):min(N, x1)] * 0.62).astype(np.uint8)


CHARACTERS = [Boopie, GPT, Codex, Klaude, Whale, Doubao]


# ---------------------------------------------------------------- the expressions
def wave(t, period, lo=-1.0, hi=1.0):
    return lo + (hi - lo) * (0.5 + 0.5 * math.sin(2 * math.pi * t / period))


def pose_for(name: str, t: float, length: float) -> Pose:
    bob = round(wave(t, 1.5, 0, 1))
    blink = (t % 3.0) > 2.75
    p = Pose(t=t, light=LIGHT[name], accent=ACCENT[name])
    p.sparkle_speed = {"thinking": 2.8, "listening": 1.2, "speaking": 1.5, "working": 2.0}.get(name, 0.6)
    if name == "idle":
        p.dy = bob
        p.eyes = "blink" if blink else "open"
        p.antenna = 18 + 8 * math.sin(2 * math.pi * t / 3)
        p.light_level = wave(t, 3, 0.75, 1)
    elif name == "boot":
        if t < 0.5:            # just the light, blinking on
            p.scale = 0.3
            p.feet = False
            p.hands = ()
            p.light_level = 1.0 if int(t * 10) % 2 else 0.2
            p.eyes = "blink"
        else:
            k = min(1.0, (t - 0.5) / 0.4)
            over = math.sin(min(1.0, (t - 0.5) / 0.6) * math.pi) * 0.15
            p.scale = 0.3 + 0.7 * k + over
            p.squash = 1.0 + over
            p.eyes = "blink" if t < 1.2 else "open"
            p.mouth = "w" if t < 1.4 else "smile"
            if 1.2 <= t < 1.6:
                p.fx = [("spark", 14, 18), ("spark", 50, 16)]
    elif name == "listening":
        p.eyes = "wide"
        p.antenna = 0
        p.antenna_len = 10
        p.tilt = -0.06
        p.hands = ((-1, 3), (1, -4))
        p.mouth = "smile"
        p.light_level = wave(t, 0.4, 0.6, 1)
        p.rings, p.level = 0.9, wave(t, 0.4, 0.3, 0.8)
        r = (t / length * 3) % 1
        for k in range(2):
            rr = 3 + (r + k * 0.5) % 1 * 6
            for a in range(-40, 41, 20):
                p.fx.append(("lpx", 2 + rr * math.cos(math.radians(a)), rr * math.sin(math.radians(a)), (110, 190, 255)))
    elif name == "thinking" and t >= WORKING_AFTER:   # typing away on a tiny laptop, bits of code flying up
        t = (t - WORKING_AFTER) % WORKING_LOOP
        tap = int(t / 0.15) % 2
        p.eyes = "look"
        p.look = (0, 1)
        p.mouth = "flat" if int(t / 0.6) % 2 else "w"
        p.prop = "laptop"
        p.hands = (("front", "body", -6, 14 - tap), ("front", "body", 6, 13 + tap))
        p.antenna = 12 + 4 * tap
        p.light_level = 0.6 + 0.4 * tap
        for i in range(2):
            k = (t / WORKING_LOOP + i / 2) % 1
            p.fx.append(("bicon", "code", -16 + i * 27 + round(k * 3), 4 - k * 22))
    elif name == "thinking":
        length = WORKING_AFTER
        p.eyes = "look"
        p.look = (-2, -1)
        p.mouth = "flat"
        p.hands = ((-1, 3), ("front", "face", 4, 7))
        p.antenna = 10
        orbit = t / 0.8 * 2 * math.pi
        p.fx = [("lpx", 5 * math.cos(orbit), 3 * math.sin(orbit), WHITE)]
        for i in range(int(t / length * 4) % 4):
            p.fx.append(("icon", "dot", 6 + i * 5, 20 - i * 2))
    elif name == "speaking":
        level = abs(math.sin(t * 7.3)) * 0.6 + abs(math.sin(t * 3.1)) * 0.4
        p.mouth = "talk"
        p.talk = int(level * 3.4)
        p.dy = bob
        p.eyes = "blink" if (t % 1.2) > 1.1 else "open"
        p.hands = ((-1, 3 - round(level * 3)), (1, 3))
        p.light_level = 0.55 + 0.45 * level
        p.rings, p.level = 0.6, level
        if (t % 1.2) < 0.6:
            p.fx = [("icon", "note", 52, 24 - int(t % 0.6 * 10))]
    elif name == "error":
        p.eyes = "worried"
        p.mouth = "wavy"
        p.antenna = 70
        p.antenna_len = 8
        p.light_level = 1.0 if int(t * 4) % 2 else 0.25
        p.fx = [("ficon", "drop", 13, -10 + int(t / length * 6))]
        if int(t * 2) % 2 == 0:
            p.fx.append(("icon", "excl", 8, 14))
    elif name == "off":
        if t < 1.2:
            up = wave(t, 0.4, 0, 1)
            p.hands = ((-1, 3), (1, -4 - round(up * 3)))
            p.eyes = "happy"
            p.mouth = "smile"
        else:
            k = min(1.0, (t - 1.2) / 0.8)
            p.eyes = "blink"
            p.mouth = "w"
            p.squash = 1.0 + 0.12 * k
            p.antenna = 18 + 40 * k
            p.light_level = 1 - k
            p.aura = 0.75 * (1 - k)
            p.dim = 1 - 0.55 * k
    elif name == "happy":
        k = t / length
        if k < 0.12:
            p.squash = 1.2
        elif k < 0.5:
            s = (k - 0.12) / 0.38
            p.dy = -4 * math.sin(s * math.pi)
            p.squash = 0.9
            p.feet = s < 0.1 or s > 0.9
        elif k < 0.62:
            p.squash = 1.15
        p.eyes = "happy"
        p.mouth = "o" if k < 0.62 else "smile"
        p.hands = ((-1, -3), (1, -3)) if 0.12 <= k < 0.62 else ((-1, 3), (1, 3))
        p.antenna = 18 + 25 * math.sin(k * 18) * (1 - k)
        p.blush = "big"
        for i, x0 in enumerate((6, 52)):
            y = 30 - k * 22 - i * 3
            if y > 2:
                p.fx.append(("icon", "heart", x0, y))
    elif name == "hungry":
        p.eyes = "look"
        p.look = (1, -1)
        p.mouth = "wavy"
        p.hands = ((-1, 3), ("front", "body", -2, 9))
        p.antenna = 30
        p.light_level = wave(t, 1.2, 0.5, 0.9)
        p.fx = [("icon", "bubble", 6, 9), ("icon", "bowl", 6, 10)]
        if int(t * 2) % 2:     # a rumble by the tummy
            for i in range(3):
                p.fx.append(("px", 9 - i, 46 + (i % 2), (200, 195, 225)))
    elif name == "eating":
        chomp = int(t / 0.3) % 2
        p.eyes = "happy"
        p.mouth = "chomp"
        p.talk = chomp
        p.hands = (("front", "face", -5, 8), ("front", "face", 5, 8))
        p.fx = [("ficon", "cookie", -3, 4 + (0 if chomp else 1))]
        crumb = (t % 0.6) / 0.6
        p.fx += [("fpx", -6 + int(crumb * 2), 10 + crumb * 8, (214, 150, 80)),
                 ("fpx", 4 - int(crumb * 2), 11 + crumb * 7, (214, 150, 80))]
        p.blush = "big"
    elif name == "sleepy":
        breath = wave(t, 3, 0, 1)
        p.eyes = "blink"
        p.mouth = "o" if breath > 0.7 else "w"
        p.squash = 1.0 + 0.06 * breath
        p.antenna = 55 + 8 * math.sin(2 * math.pi * t / 3)
        p.light_level = 0.35
        k = t / length
        for i in range(3):
            kk = (k + i / 3) % 1
            p.fx.append(("icon", "z", 52 + kk * 7, 30 - kk * 26))
    elif name == "sad":
        p.eyes = "worried"
        p.mouth = "frown"
        p.dy = 1
        p.antenna = 60
        p.light_level = 0.55
        k = t / length
        p.fx = [("ficon", "drop", -9, 1 + int(k * 10))] if k < 0.7 else []
    elif name == "dizzy":
        s = math.sin(2 * math.pi * t / length)
        p.dx = round(2 * s)
        p.tilt = 0.08 * s
        p.eyes = "spiral"
        p.spin = int(t * 8)
        p.mouth = "wavy"
        p.antenna = 18 + 35 * s
        for i in range(3):
            a = 2 * math.pi * (t / 0.8 + i / 3)
            p.fx.append(("spark", 32 + 13 * math.cos(a), 9 + 3 * math.sin(a), GOLD, 1))
    return p


def overlay(p: Pose, name: str, t: float, length: float, food: str = "rice") -> Pose:
    """Lays an overlay over a pose: shared by every character, drawn in any expression."""
    if name == "surprise":
        k = t / length
        if k < 0.35:
            p.dy -= 3 * math.sin(k / 0.35 * math.pi)
        if p.eyes not in ("blink", "x", "spiral"):
            p.eyes = "wide"
        if int(t * 5) % 2 == 0:
            p.fx.append(("icon", "excl", 8, 12))
    elif name == "blush":
        p.blush = "big"
        if (t % length) > length * 0.6:
            p.fx.append(("icon", "heart", 50, 10 - int((t % length - length * 0.6) * 8)))
    elif name == "confetti":
        rng = np.random.default_rng(7)
        for _ in range(14):
            x = rng.integers(4, 60)
            y = (rng.integers(0, 64) + t * 30) % 64
            r2, g2, b2 = colorsys.hsv_to_rgb(rng.random(), 0.7, 1)
            p.fx.append(("px", x, y, (round(r2 * 255), round(g2 * 255), round(b2 * 255))))
    elif name == "hearts":
        k = t / length
        for i, x0 in enumerate((6, 52)):
            y = 30 - k * 22 - i * 3
            if y > 2:
                p.fx.append(("icon", "heart", x0, y))
    elif name == "low_battery":
        if int(t * 2) % 2:
            p.fx.append(("icon", "battery", 50, 6))
            p.light, p.light_level = RED, min(p.light_level, 0.6)
    elif name == "food":   # something to eat, bobbing to be tapped
        bob = int(wave(t, 1.6, 0, 2))
        rows = ICONS[food][0]
        p.fx.append(("icon", food, 49 + (11 - len(rows[0])) // 2, 55 - len(rows) + bob))
    elif name == "charging":
        p.fx.append(("icon", "bolt", 53, 4 + int(wave(t, 0.8, 0, 2))))
        for i in range(3):
            k = (t / length + i / 3) % 1
            p.fx.append(("px", 8 + i * 24, 54 - k * 36, (255, 230, 90)))
    return p


# ---------------------------------------------------------------- rendering
def aura(c: Canvas, cx, cy, radius, strength, acc):
    """Muse's glow: a dithered disc fading out from the centre, in two tints of the accent."""
    if strength <= 0:
        return
    bay = BAYER[np.arange(N)[:, None] % 4, np.arange(N)[None, :] % 4]
    d = np.sqrt((XS - cx) ** 2 + ((YS - cy) * 1.1) ** 2) / radius
    i = np.clip(1 - d, 0, 1) * strength
    a1 = tuple(round(v * 0.16) for v in acc)
    a2 = tuple(round(v * 0.34) for v in acc)
    inside = d < 1
    c.img[inside & (i > (bay * 0.9))] = a1
    c.img[inside & (i > 0.55 + bay * 0.35)] = a2


def rings(c: Canvas, cx, cy, t, level, speed, acc):
    """Expanding dotted rings, as Muse's while listening and speaking."""
    for k in range(2):
        ph = (t * speed + k * 0.5) % 1
        r = 20 + ph * 11
        fade = (1 - ph) * (0.35 + level)
        n = int(r * 2.2)
        for j in range(n):
            ang = j * 2 * math.pi / n
            x, y = round(cx + math.cos(ang) * r), round(cy + math.sin(ang) * r * 0.92)
            if 0 <= x < N and 0 <= y < N and BAYER[y % 4][x % 4] < fade and not c.body[y, x]:
                c.put(x, y, acc if fade > 0.6 else tuple(round(v * 0.34) for v in acc))


def sparkles(c: Canvas, cx, cy, t, speed, front, acc, count=6):
    spk = mix(acc, WHITE, 0.45)
    for i in range(count):
        ang = t * speed + i * 2 * math.pi / count
        s = math.sin(ang)
        if (s > 0) != front:
            continue
        rr = 25 + 2 * math.sin(i * 1.9 + t * 0.7)
        x, y = round(cx + math.cos(ang) * rr), round(cy - 3 + s * rr * 0.42)
        tw = 0.5 + 0.5 * math.sin(t * 5 + i * 1.7)
        arm = acc if front else tuple(round(v * 0.34) for v in acc)
        if tw > 0.8:
            c.spark(x, y, WHITE if front else spk, 0)
            for k in (1, 2):
                for dx, dy in ((k, 0), (-k, 0), (0, k), (0, -k)):
                    c.put(x + dx, y + dy, spk if k == 1 and front else arm)
        elif tw > 0.45:
            c.spark(x, y, arm, 1)
        elif tw > 0.15:
            c.put(x, y, arm)


def render(rig: Rig, pose: Pose) -> np.ndarray:
    c = Canvas()
    c.eye = rig.eye_colour
    acc = pose.accent
    t = pose.t
    # background layers, as Muse's: the glow, the ground shadow, sparkles behind
    aura(c, 32, 37, 29 + pose.level * 4 + math.sin(t * 1.5), pose.aura + pose.level * 0.4, acc)
    for dx in range(-12, 13):
        if abs(dx) < 12 - (dx % 2):
            c.put(32 + dx, 59, (22, 18, 34))
    scene_back(c, pose.scene, t, acc)
    if rig.skin:
        skin_back(c, rig.skin, t)
    sparkles(c, 32, 40, t, pose.sparkle_speed, False, acc)
    c.rim = mix(acc, WHITE, 0.2)
    anchors = rig.draw(c, pose)
    if rig.skin:
        skin_body(c, rig.skin, pose, anchors)
    if pose.rings:
        rings(c, 32, anchors["face"][1] + 2, t, pose.level, pose.rings, acc)
    fx, fy = anchors["face"]
    if anchors.get("show_face", True):
        rig.face(c, round(fx), round(fy), pose)
        if rig.skin:
            skin_face(c, rig.skin, pose, anchors)
    for item in pose.wear:
        if "slots" in anchors:
            wear(c, item, anchors["slots"])
    bx, by = anchors["body"]
    if pose.prop == "laptop":
        c.icon("laptop", round(bx) - 7, round(by) + 8)
    front = np.zeros((N, N), dtype=bool)
    for h in pose.hands:
        if h[0] == "front":
            ax, ay = anchors[h[1]]
            front |= ((XS - ax - h[2]) / 2.7) ** 2 + ((YS - ay - h[3]) / 2.2) ** 2 <= 1
    if front.any():
        hr = rig.hand_ramp()
        c.flat(front, hr["light"])
        c.outline(front, hr["out"])
    sparkles(c, 32, 40, t, pose.sparkle_speed, True, acc)
    scene_front(c, pose.scene, t)
    lx, ly = anchors["light"]
    for e in pose.fx:
        kind = e[0]
        if kind == "spark":
            c.spark(*e[1:])
        elif kind == "px":
            c.put(*e[1:])
        elif kind == "lpx":
            c.put(lx + e[1], ly + e[2], e[3])
        elif kind == "fpx":
            c.put(fx + e[1], fy + e[2], e[3])
        elif kind == "icon":
            c.icon(e[1], e[2], e[3])
        elif kind == "ficon":
            c.icon(e[1], round(fx + e[2]), round(fy + e[3]))
        elif kind == "bicon":
            c.icon(e[1], round(bx + e[2]), round(by + e[3]))
    out = scene_post(c.img, pose.scene, t)
    if pose.dim < 1:
        out = (out * pose.dim).astype(np.uint8)
    return out


def frames(rig, name, length, scale, over=None):
    """An expression's loop, or with over=(name, length) an overlay's loop on top of idle."""
    n = max(1, round((over[1] if over else length) * FPS))
    out = []
    for i in range(n):
        t = i / FPS
        p = pose_for(name, t, length)
        if over:
            p = overlay(p, over[0], t, over[1])
        out.append(Image.fromarray(render(rig, p)).resize((N * scale, N * scale), Image.NEAREST))
    return out


def font(size):
    for path in ("/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc", "/System/Library/Fonts/PingFang.ttc"):
        try:
            return ImageFont.truetype(path, size)
        except OSError:
            pass
    return ImageFont.load_default()


def grid(cells, labels, cols, cell, title_font):
    """Animated grid: cells is a list of frame lists, one per label."""
    lab = 30
    rows = (len(cells) + cols - 1) // cols
    total = max(len(f) for f in cells)
    out = []
    for i in range(total):
        sheet = Image.new("RGB", (cols * (cell + 8) + 8, rows * (cell + lab + 8) + 8), (22, 20, 30))
        d = ImageDraw.Draw(sheet)
        for k, (fr, text) in enumerate(zip(cells, labels)):
            x, y = 8 + (k % cols) * (cell + 8), 8 + (k // cols) * (cell + lab + 8)
            d.text((x + 2, y), text, fill=(220, 215, 240), font=title_font)
            sheet.paste(fr[i % len(fr)].resize((cell, cell), Image.NEAREST), (x, y + lab))
        out.append(sheet)
    return out


def save_gif(frames_, path):
    frames_[0].save(path, save_all=True, append_images=frames_[1:], duration=1000 // FPS, loop=0)


TELLING = {"boot": 0.8, "happy": 0.3, "off": 0.3, "surprise": 0.15, "confetti": 0.25, "low_battery": 0.8,
           "blush": 0.8}


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--character", default="all", help="boopie, gpt, codex, klaude, whale, doubao or all")
    ap.add_argument("--color", help="body colour, RRGGBB (one character only)")
    ap.add_argument("--scale", type=int, default=4)
    args = ap.parse_args()
    rigs = [r for r in CHARACTERS if args.character in ("all", r.key)]
    f_title = font(20)
    family = {}
    for R in rigs:
        rig = R(args.color if args.character != "all" else None)
        out = args.out / rig.key
        out.mkdir(parents=True, exist_ok=True)
        all_frames = []
        for name, length, _ in EXPRESSIONS:
            fr = frames(rig, name, length, args.scale)
            save_gif(fr, out / f"{name}.gif")
            all_frames.append(fr)
        family[rig.key] = (rig, all_frames)
        for name, length, _ in OVERLAYS:
            fr = frames(rig, "idle", 3.0, args.scale, (name, length))
            save_gif(fr, out / f"overlay_{name}.gif")
            all_frames.append(fr)
        views = EXPRESSIONS + [(n, ln, "+" + zh) for n, ln, zh in OVERLAYS]
        labels = [f"{zh} {name}" for name, _, zh in views]
        g = grid(all_frames, labels, 5, N * 3, f_title)
        save_gif(g, out / "all.gif")
        stills = [[fr[int(len(fr) * TELLING.get(name, 0.4))]] for fr, (name, _, _) in zip(all_frames, views)]
        grid(stills, labels, 5, N * 3, f_title)[0].save(out / "sheet.png")
    if len(family) > 1:   # every character, a row each, in a few expressions
        picks = [("idle", "待机", 0.4), ("listening", "聆听", 0.4), ("thinking", "思考", 0.2),
                 ("speaking", "说话", 0.4), ("happy", "开心", 0.3), ("thinking", "工作中", 0.8),
                 ("sleepy", "犯困", 0.4), ("dizzy", "晕了", 0.4)]
        cells, labels, at = [], [], []
        for key, (rig, all_frames) in family.items():
            for name, zh, still in picks:
                cells.append(all_frames[[e[0] for e in EXPRESSIONS].index(name)])
                labels.append(f"{rig.name} · {zh}")
                at.append(still)
        g = grid(cells, labels, len(picks), N * 3, font(16))
        save_gif(g, args.out / "family.gif")
        stills = [[fr[int(len(fr) * k)]] for fr, k in zip(cells, at)]
        grid(stills, labels, len(picks), N * 3, font(16))[0].save(args.out / "family.png")


if __name__ == "__main__":
    main()

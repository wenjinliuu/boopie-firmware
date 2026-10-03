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
]


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
    "bubble": ([".#####.", "#.....#", "#.....#", "#.....#", ".#####.", "..#....", ".#....."],
               {"#": (200, 195, 225)}),
    "code": (["#...#", "#.#.#", "#...#"], {"#": (150, 245, 200)}),
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

    def __init__(self, colour=None):
        self.rp = ramp(colour or self.colour)

    def draw(self, c: Canvas, p: Pose) -> dict:
        raise NotImplementedError

    def face(self, c: Canvas, fx, fy, p: Pose):
        for side in (-1, 1):
            eye(c, fx + side * self.eye_gap, fy, p, side)
        blush(c, fx, fy, p)
        mouth(c, fx + p.look[0] // 2, fy + 5, p)

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


class Boopie(Rig):
    """布比: a round mochi sprite, one antenna leaning right with a glowing bulb."""
    key, name, colour = "boopie", "布比", "ff9ec8"

    def draw(self, c, p):
        cx, cy, rx, ry = 32, 40, 17, 15
        f = self.frame(p, cx, cy + ry)
        body = f.ellipse(cx, cy, rx, ry)
        if p.feet and p.scale > 0.6:
            body |= f.ellipse(cx - 7, cy + ry - 1, 4, 2.5) | f.ellipse(cx + 7, cy + ry - 1, 4, 2.5)
        for h in p.hands:
            if h[0] != "front":
                body |= f.ellipse(cx + h[0] * rx, cy + h[1], 3 / p.squash, 2.6 * p.squash)
        c.shaded(body, self.rp)
        c.outline(body, self.rp["out"])
        light = antenna(c, f, cx + 3, cy - ry + 1, p, self.rp["out"], 2.4, self.light_colour(p), self.rp["out"])
        return {"slots": {"hat": f.pt(27, 27), "eyes": (*f.pt(cx, cy - 1), 7 * f.sx), "neck": (*f.pt(cx, 47), 26 * f.sx)}, "face": f.pt(cx, cy - 1), "body": f.pt(cx, cy), "light": light, "show_face": p.scale > 0.6}


class Codex(Rig):
    """Codex: a cloud-headed robot whose face is a terminal; its eyes are the prompt, >_ ."""
    key, name, colour = "codex", "Codex", "5b86f5"
    size, squash_k, jump_k = 1.08, 0.35, 0.4
    screen = (30, 34, 84)
    glyph = (120, 236, 240)

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
        return {"slots": {"hat": f.pt(31, 10), "eyes": (face[0] - 0.5, face[1], 4.5 * f.sx), "neck": (*f.pt(32, 38), 16 * f.sx)}, "face": face, "body": f.pt(32, 40), "light": f.pt(44, 14), "show_face": p.scale > 0.6}

    def face(self, c, fx, fy, p):
        col = p.light or self.glyph
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


class GPT(Rig):
    """GPT: a white mochi, Boopie's shape and face, with the knot of its logo
    clipped on its head like a hair clip."""
    key, name, colour = "gpt", "GPT", "e8e8e8"
    ink = (18, 18, 24)
    hole = (34, 34, 44)

    def __init__(self, colour=None):
        super().__init__(colour)
        self.rp["out"] = self.ink
        self.bands = ramp("f6f6f6")
        self.bands["out"] = self.ink

    def draw(self, c, p):
        cx, cy, rx, ry = 32, 41, 16, 14
        f = self.frame(p, cx, cy + ry)
        body = f.ellipse(cx, cy, rx, ry)
        if p.feet and p.scale > 0.6:
            body |= f.ellipse(cx - 7, cy + ry - 1, 4, 2.5) | f.ellipse(cx + 7, cy + ry - 1, 4, 2.5)
        for h in p.hands:
            if h[0] != "front":
                body |= f.ellipse(cx + h[0] * rx, cy + h[1], 3, 2.6)
        c.shaded(body, self.rp)
        c.outline(body, self.ink)
        n, kx, ky = len(KNOT), 42, 25          # the clip, up on the right like Boopie's antenna
        gx = np.floor(f.rx - (kx - n / 2)).astype(int)
        gy = np.floor(f.ry - (ky - n / 2)).astype(int)
        ok = (gx >= 0) & (gx < n) & (gy >= 0) & (gy < n)
        cells = np.full((N, N), " ")
        cells[ok] = KNOT_GRID[gy[ok], gx[ok]]
        bands, holes = cells == "#", cells == "."
        c.flat(holes, self.hole)
        rim, c.rim = c.rim, None               # the bands are too thin for a rim light
        c.shaded(bands, self.bands)
        c.rim = rim
        c.outline(bands | holes, self.ink)
        return {"slots": {"hat": f.pt(25, 29), "eyes": (*f.pt(cx, cy - 1), 7 * f.sx), "neck": (*f.pt(cx, 48), 24 * f.sx)}, "face": f.pt(cx, cy - 1), "body": f.pt(cx, cy), "light": f.pt(42, 17), "show_face": True}


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

    def draw(self, c, p):
        cx, base = 32, 51
        f = self.frame(p, cx, base)
        body = f.rect(17, 22, 47, 44)
        for h in p.hands:
            if h[0] != "front":
                up = min(0, h[1]) * 1.6
                x0 = 11 if h[0] < 0 else 47
                body |= f.rect(x0, 31 + up, x0 + 6, 36 + up)
        if p.feet and p.scale > 0.6:
            for x in (20, 25, 36, 41):
                body |= f.rect(x, 44, x + 3, 51)
        c.shaded(body, self.rp)
        c.outline(body, self.rp["out"])
        return {"slots": {"hat": f.pt(32, 23), "eyes": (*f.pt(32, 30), 7 * f.sx), "neck": (*f.pt(32, 42), 30 * f.sx)}, "face": f.pt(32, 30), "body": f.pt(32, 35), "light": f.pt(44, 16), "show_face": p.scale > 0.6}

    def face(self, c, fx, fy, p):
        eyec = mix(WHITE, p.light, 0.35) if p.light and p.light != WHITE else (255, 246, 236)
        c.eye, c.shine = eyec, None
        for side in (-1, 1):
            eye(c, fx + side * 7, fy, p, side, square=True)
        blush(c, fx, fy, p, 11, (255, 176, 150))
        if p.mouth in ("talk", "o", "chomp", "wavy", "frown"):
            mouth(c, fx, fy + 6, p, colour=(92, 34, 22), inside=(170, 60, 50))


class Whale(Rig):
    """DeepSeek 小鲸鱼: a round little whale with a white tummy and a perky tail;
    its spout is the state light."""
    key, name, colour = "whale", "DeepSeek 小鲸鱼", "5a7dff"
    belly = ((214, 224, 255), (240, 244, 255))

    def draw(self, c, p):
        cx, cy, rx, ry = 31, 41, 16, 15
        f = self.frame(p, cx, cy + ry)
        a = math.radians(p.antenna - 18)            # the tail swings like Boopie's antenna
        tx, ty = 46 + 3 * math.sin(a), 21 - 1.5 * math.cos(a)
        tail = f.ellipse(44 + 1.5 * math.sin(a), 29, 2.6, 5) | f.ellipse(tx - 3, ty, 3.5, 2) | f.ellipse(tx + 3, ty - 0.5, 3.5, 2)
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
        return {"slots": {"hat": f.pt(37, 27), "eyes": (*f.pt(cx, cy - 2), 7 * f.sx), "neck": (*f.pt(cx, 47), 24 * f.sx)}, "face": f.pt(cx, cy - 2), "body": f.pt(cx, cy), "light": (hx, hy - h - 1),
                "show_face": p.scale > 0.6}


class Doubao(Rig):
    """豆包: a girl with a brown bob and big eyes, in a black top; her hair clip is the state light."""
    key, name, colour = "doubao", "豆包", "f2c9b4"
    size, squash_k, jump_k = 1.08, 0.35, 0.4
    hair = "6b4a3e"
    top = "3a3a44"

    def __init__(self, colour=None):
        super().__init__(colour)
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
        return {"slots": {"hat": f.pt(29, 11), "eyes": (*f.pt(32, 30), 6 * f.sx), "neck": (*f.pt(32, 43), 12 * f.sx)}, "face": f.pt(32, 30), "body": f.pt(32, 44), "light": f.pt(45, 16), "show_face": p.scale > 0.6}

    def face(self, c, fx, fy, p):
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
}
ACCESSORIES = {   # name: slot, Chinese name
    "bow": ("hat", "蝴蝶结"), "party_hat": ("hat", "生日帽"), "crown": ("hat", "小皇冠"),
    "scarf": ("neck", "红围巾"),
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
        p.fx = [("icon", "bubble", 52, 3), ("icon", "bowl", 52, 4)]
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


def overlay(p: Pose, name: str, t: float, length: float) -> Pose:
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
    acc = pose.accent
    t = pose.t
    # background layers, as Muse's: the glow, the ground shadow, sparkles behind
    aura(c, 32, 37, 29 + pose.level * 4 + math.sin(t * 1.5), pose.aura + pose.level * 0.4, acc)
    for dx in range(-12, 13):
        if abs(dx) < 12 - (dx % 2):
            c.put(32 + dx, 59, (22, 18, 34))
    sparkles(c, 32, 40, t, pose.sparkle_speed, False, acc)
    c.rim = mix(acc, WHITE, 0.2)
    anchors = rig.draw(c, pose)
    if pose.rings:
        rings(c, 32, anchors["face"][1] + 2, t, pose.level, pose.rings, acc)
    fx, fy = anchors["face"]
    if anchors.get("show_face", True):
        rig.face(c, round(fx), round(fy), pose)
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
    out = c.img
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

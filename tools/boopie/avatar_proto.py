#!/usr/bin/env python3
# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0

"""Boopie's pixel avatar, as a design prototype: draws every expression in
esp32/components/boopie/boopie_expr.h on the same 64 x 64 grid the firmware
uses, and writes previews.

  python3 tools/boopie/avatar_proto.py --out previews/ [--color ff9ec8]

writes one GIF per expression, all.gif (every expression, animated, in a
grid) and sheet.png (one frame of each). The firmware renderer will be a C
port of this file. Needs Pillow and NumPy.

Boopie: a round mochi sprite with one antenna leaning right, a glowing bulb
on its tip (its "boop button", and a status light that takes the state's
colour), little round hands and two nub feet. The body takes any colour; the
shading, outline and glow come from it.
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
BX, BY = 32, 40          # body centre at rest
RX, RY = 17, 15          # body radii at rest
FPS = 12
BAYER = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]) / 16.0
YS, XS = np.mgrid[0:N, 0:N] + 0.5

EYE = (30, 22, 46)
WHITE = (255, 255, 255)
CHEEK = (255, 122, 152)
TEAR = (120, 190, 255)
GOLD = (255, 206, 84)
RED = (255, 84, 96)

# State light on the antenna's bulb; None takes the body's own glow.
LIGHT = {
    "boot": WHITE, "idle": None, "listening": (110, 190, 255), "thinking": (200, 130, 255),
    "speaking": (110, 235, 170), "error": RED, "off": (90, 84, 110), "happy": (255, 222, 90),
    "hungry": (255, 170, 80), "eating": (255, 190, 110), "sleepy": (120, 112, 170),
    "sad": (130, 160, 230), "surprised": WHITE, "dizzy": (255, 200, 120), "celebrate": None,
    "shy": (255, 140, 180), "low_battery": RED, "charging": (255, 230, 90),
}

# Name, seconds per loop, Chinese label. Order and names as boopie_expr.h.
EXPRESSIONS = [
    ("boot", 2.0, "开机"), ("idle", 3.0, "待机"), ("listening", 1.6, "聆听"),
    ("thinking", 2.4, "思考"), ("speaking", 1.2, "说话"), ("error", 2.0, "出错"),
    ("off", 2.4, "关机"), ("happy", 1.2, "开心"),
    ("hungry", 2.4, "饿了"), ("eating", 1.2, "吃东西"), ("sleepy", 3.0, "犯困"),
    ("sad", 2.4, "难过"), ("surprised", 1.2, "惊讶"), ("dizzy", 1.6, "晕了"),
    ("celebrate", 1.6, "庆祝"), ("shy", 2.4, "害羞"), ("low_battery", 2.0, "没电了"),
    ("charging", 1.6, "充电中"),
]


def ramp(hex_colour: str) -> dict:
    r, g, b = (int(hex_colour[i:i + 2], 16) / 255 for i in (0, 2, 4))
    h, l, s = colorsys.rgb_to_hls(r, g, b)

    def c(dl, ds=0.0, dh=0.0):
        rr, gg, bb = colorsys.hls_to_rgb((h + dh) % 1, min(1, max(0, l + dl)), min(1, max(0, s + ds)))
        return (round(rr * 255), round(gg * 255), round(bb * 255))
    return {"out": c(-0.52, 0.1, 0.03), "dark": c(-0.16, 0.05, 0.02), "mid": c(0), "light": c(0.1),
            "high": c(0.2, -0.1), "glow": c(0.16, 0.25, -0.03)}


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
}


@dataclass
class Pose:
    dx: float = 0.0            # body offset
    dy: float = 0.0
    squash: float = 1.0        # > 1 wider and shorter
    scale: float = 1.0
    tilt: float = 0.0          # x shear per row above the centre
    feet: bool = True
    hands: tuple = ((-1, 3), (1, 3))   # (side, dy) at the body's edge; or ("front", x, y)
    antenna: float = 18.0      # degrees right of vertical
    antenna_len: float = 9.0
    bulb: float = 2.4
    light: tuple | None = None
    light_level: float = 1.0
    eyes: str = "open"         # open blink happy wide half spiral x worried down look
    look: tuple = (0, 0)
    mouth: str = "w"           # w smile o talk frown wavy flat chomp none
    talk: int = 1
    blush: str = "normal"      # normal big none
    spin: int = 0              # spiral eye phase
    fx: list = field(default_factory=list)   # (icon, x, y) or ("spark", x, y, colour) or ("px", x, y, colour)
    dim: float = 1.0


# ---------------------------------------------------------------- drawing
class Canvas:
    def __init__(self, pal):
        self.pal = pal
        self.img = np.zeros((N, N, 3), dtype=np.uint8)
        self.solid = np.zeros((N, N), dtype=bool)

    def put(self, x, y, colour):
        x, y = int(round(x)), int(round(y))
        if 0 <= x < N and 0 <= y < N:
            self.img[y, x] = self.pal.get(colour, colour) if isinstance(colour, str) else colour
            self.solid[y, x] = True

    def shaded(self, mask, light=(-0.55, -0.83)):
        """Fill mask with the body ramp, lit from the top left by the slope of its blur."""
        h = mask.astype(float)
        for _ in range(3):
            p = np.pad(h, 2, mode="edge")
            h = sum(p[dy:dy + N, dx:dx + N] for dy in range(5) for dx in range(5)) / 25
        gy, gx = np.gradient(h)
        d = (-gx * light[0] - gy * light[1]) * 10 + (h - 0.95) + (BAYER[np.arange(N)[:, None] % 4, np.arange(N)[None, :] % 4] - 0.5) * 0.18
        for role, cond in (("dark", d <= -0.3), ("mid", (d > -0.3) & (d <= 0.2)),
                           ("light", (d > 0.2) & (d <= 0.55)), ("high", d > 0.55)):
            sel = mask & cond
            self.img[sel] = self.pal[role]
        self.solid |= mask

    def outline(self, mask):
        ring = np.zeros_like(mask)
        ring[1:] |= mask[:-1]
        ring[:-1] |= mask[1:]
        ring[:, 1:] |= mask[:, :-1]
        ring[:, :-1] |= mask[:, 1:]
        ring &= ~mask
        self.img[ring] = self.pal["out"]
        self.solid |= ring

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

    def disc(self, cx, cy, r, colour):
        m = (XS - cx) ** 2 + (YS - cy) ** 2 <= r * r + 0.3
        self.img[m] = colour
        self.solid |= m
        return m


def ellipse(cx, cy, rx, ry, xs=XS, ys=YS):
    return ((xs - cx) / rx) ** 2 + ((ys - cy) / ry) ** 2 <= 1


def mix(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))


def render(pose: Pose, pal: dict) -> np.ndarray:
    c = Canvas(pal)
    rx = RX * pose.squash * pose.scale
    ry = RY / pose.squash * pose.scale
    cx, cy = BX + pose.dx, BY + pose.dy + (RY - ry)       # squashing keeps the feet down
    xs = XS + pose.tilt * (YS - cy)                         # shear about the centre
    body = ellipse(cx, cy, rx, ry, xs)
    if pose.feet and pose.scale > 0.6:
        fy = cy + ry - 1
        body |= ellipse(cx - 7, fy, 4, 2.5, xs) | ellipse(cx + 7, fy, 4, 2.5, xs)
    front = np.zeros_like(body)
    for hand in pose.hands:
        if hand[0] == "front":
            front |= ellipse(cx + hand[1], cy + hand[2], 2.7, 2.2)
        else:
            side, hy = hand
            body |= ellipse(cx + side * rx, cy + hy, 3, 2.6, xs)
    c.shaded(body)
    c.outline(body)
    if front.any():          # hands in front of the body: flat and light, outlined
        c.img[front] = pal["light"]
        c.solid |= front
        c.outline(front)

    # antenna: a stem from the top, leaning right, bulb on the tip
    top_x = cx + 3 - pose.tilt * ry
    top_y = cy - ry + 1
    a = math.radians(pose.antenna)
    steps = int(pose.antenna_len)
    for i in range(steps + 1):
        bend = a * (i / steps) ** 1.3
        c.put(top_x + math.sin(bend) * i * 0.95, top_y - math.cos(bend) * i, "out")
    tip_x = top_x + math.sin(a) * pose.antenna_len * 0.95
    tip_y = top_y - math.cos(a) * pose.antenna_len - pose.bulb
    light = pose.light or pal["glow"]
    light = mix((20, 16, 30), light, max(0.0, min(1.0, pose.light_level)))
    c.disc(tip_x, tip_y, pose.bulb + 1, pal["out"])
    c.disc(tip_x, tip_y, pose.bulb, light)
    if pose.light_level > 0.5:
        c.put(tip_x - 1, tip_y - 1, WHITE)

    if pose.scale > 0.6:
        face(c, cx - pose.tilt * 1, cy, pose)
    for f in pose.fx:
        if f[0] == "spark":
            c.spark(*f[1:])
        elif f[0] == "px":
            c.put(*f[1:])
        else:
            c.icon(*f)
    out = c.img
    if pose.dim < 1:
        out = (out * pose.dim).astype(np.uint8)
    return out


def face(c: Canvas, cx, cy, p: Pose):
    ey = cy - 1 + p.look[1]
    for side in (-1, 1):
        ex = cx + side * 7 + p.look[0]
        eye(c, ex, ey, p, side)
    blush = {"normal": 1, "big": 2, "none": 0}[p.blush]
    for side in (-1, 1):
        for dx in range(-blush, blush + 1):
            if blush:
                c.put(cx + side * 11 + dx, cy + 3, CHEEK)
                if blush == 2:
                    c.put(cx + side * 11 + dx, cy + 4, CHEEK)
    mouth(c, cx + p.look[0] // 2, cy + 4, p)


def eye(c: Canvas, ex, ey, p: Pose, side):
    k = p.eyes
    if k == "blink":
        for dx in range(-1, 2):
            c.put(ex + dx, ey, EYE)
    elif k == "happy":
        for dx, dy in ((-2, 1), (-1, 0), (0, -1), (1, 0), (2, 1)):
            c.put(ex + dx, ey + dy, EYE)
    elif k == "down":     # shy: eyes curved down, looking at the floor
        for dx, dy in ((-2, 0), (-1, 1), (0, 1), (1, 1), (2, 0)):
            c.put(ex + dx, ey + dy, EYE)
    elif k == "x":
        for d in range(-2, 3):
            c.put(ex + d, ey + d, EYE)
            c.put(ex + d, ey - d, EYE)
    elif k == "spiral":       # a ring whose gap goes round, and a dot
        ring = [(-1, -2), (0, -2), (1, -2), (2, -1), (2, 0), (2, 1), (1, 2), (0, 2), (-1, 2), (-2, 1), (-2, 0), (-2, -1)]
        gap = (p.spin * 3 + (side > 0) * 6) % len(ring)
        for i, (x, y) in enumerate(ring):
            if i not in (gap, (gap + 1) % len(ring)):
                c.put(ex + x, ey + y, EYE)
        c.put(ex, ey, EYE)
    elif k == "half":          # heavy lids: a flat top, the bottom of the eye under it
        for dx in range(-2, 2):
            c.put(ex + dx, ey, EYE)
        for dx in range(-1, 2):
            c.put(ex + dx, ey + 1, EYE)
            c.put(ex + dx, ey + 2, EYE if dx == 0 else EYE)
    elif k == "wide":
        for dy in range(-3, 4):
            for dx in range(-2, 2):
                if not (abs(dy) == 3 and dx in (-2, 1)):
                    c.put(ex + dx, ey + dy, EYE)
        c.put(ex - 1, ey - 2, WHITE)
        c.put(ex, ey + 1, WHITE)
    else:                 # open / worried / look
        for dy in range(-2, 3):
            for dx in range(-1, 2):
                if not (abs(dy) == 2 and dx != 0):
                    c.put(ex + dx, ey + dy, EYE)
        c.put(ex - 1, ey - 1, WHITE)
        if k == "worried":
            for i in range(3):
                c.put(ex - side * (1 - i), ey - 4 - (1 if i == 0 else 0), EYE)


def mouth(c: Canvas, mx, my, p: Pose):
    k = p.mouth
    if k == "w":
        for dx, dy in ((-2, 0), (-1, 1), (0, 0), (1, 1), (2, 0)):
            c.put(mx + dx, my + dy, EYE)
    elif k == "smile":
        for dx, dy in ((-2, 0), (-1, 1), (0, 1), (1, 1), (2, 0)):
            c.put(mx + dx, my + dy, EYE)
    elif k == "o":
        for dx, dy in ((0, 0), (-1, 1), (1, 1), (0, 2)):
            c.put(mx + dx, my + dy, EYE)
        c.put(mx, my + 1, CHEEK)
    elif k == "talk":
        h = max(0, min(3, p.talk))
        for dx in range(-1, 2):
            c.put(mx + dx, my, EYE)
            for dy in range(1, h + 1):
                c.put(mx + dx, my + dy, CHEEK if dy < h or h == 1 else EYE)
        if h:
            for dx in range(-1, 2):
                c.put(mx + dx, my + h + 1, EYE)
    elif k == "frown":
        for dx, dy in ((-2, 1), (-1, 0), (0, 0), (1, 0), (2, 1)):
            c.put(mx + dx, my + dy, EYE)
    elif k == "wavy":
        for dx, dy in ((-3, 1), (-2, 0), (-1, 1), (0, 1), (1, 0), (2, 1), (3, 1)):
            c.put(mx + dx, my + dy, EYE)
    elif k == "flat":
        for dx in range(-1, 2):
            c.put(mx + dx, my + 1, EYE)
    elif k == "chomp":
        for dx in range(-2, 3):
            c.put(mx + dx, my, EYE)
        if p.talk:
            for dx in range(-1, 2):
                c.put(mx + dx, my + 1, CHEEK)
                c.put(mx + dx, my + 2, EYE)


# ---------------------------------------------------------------- the expressions
def wave(t, period, lo=-1.0, hi=1.0):
    return lo + (hi - lo) * (0.5 + 0.5 * math.sin(2 * math.pi * t / period))


def pose_for(name: str, t: float, length: float) -> Pose:
    bob = round(wave(t, 1.5, 0, 1))
    blink = (t % 3.0) > 2.75
    p = Pose(light=LIGHT[name])
    if name == "idle":
        p.dy = bob
        p.eyes = "blink" if blink else "open"
        p.antenna = 18 + 8 * math.sin(2 * math.pi * t / 3)
        p.light_level = wave(t, 3, 0.75, 1)
    elif name == "boot":
        if t < 0.5:            # just the bulb, blinking on
            p.scale = 0.3
            p.feet = False
            p.hands = ()
            p.light_level = 1.0 if int(t * 10) % 2 else 0.2
            p.dim = 1.0
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
        r = (t / length * 3) % 1
        for k in range(2):
            rr = 3 + (r + k * 0.5) % 1 * 6
            for a in range(-40, 41, 20):
                x = 44 + rr * math.cos(math.radians(a))
                y = 14 + rr * math.sin(math.radians(a))
                p.fx.append(("px", x, y, (110, 190, 255)))
    elif name == "thinking":
        p.eyes = "look"
        p.look = (-2, -1)
        p.mouth = "flat"
        p.hands = ((-1, 3), ("front", 4, 6))
        p.antenna = 10
        orbit = t / 0.8 * 2 * math.pi
        p.fx = [("px", 44 + 5 * math.cos(orbit), 13 + 3 * math.sin(orbit), WHITE)]
        n = int(t / length * 4) % 4
        for i in range(n):
            p.fx.append(("dot", 8 + i * 5, 20 - i * 2))
    elif name == "speaking":
        level = abs(math.sin(t * 7.3)) * 0.6 + abs(math.sin(t * 3.1)) * 0.4
        p.mouth = "talk"
        p.talk = int(level * 3.4)
        p.dy = bob
        p.eyes = "blink" if (t % 1.2) > 1.1 else "open"
        p.hands = ((-1, 3 - round(level * 3)), (1, 3))
        p.light_level = 0.55 + 0.45 * level
        if (t % 1.2) < 0.6:
            p.fx = [("note", 50, 24 - int(t % 0.6 * 10))]
    elif name == "error":
        p.eyes = "worried"
        p.mouth = "wavy"
        p.antenna = 70
        p.antenna_len = 8
        p.light_level = 1.0 if int(t * 4) % 2 else 0.25
        p.fx = [("drop", 46, 28 + int(t / length * 6))]
        if int(t * 2) % 2 == 0:
            p.fx.append(("excl", 12, 16))
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
            p.dim = 1 - 0.55 * k
    elif name == "happy":
        k = t / length
        if k < 0.12:
            p.squash = 1.2
        elif k < 0.5:
            s = (k - 0.12) / 0.38
            p.dy = -6 * math.sin(s * math.pi)
            p.squash = 0.9
            p.feet = s < 0.1 or s > 0.9
        elif k < 0.62:
            p.squash = 1.15
        p.eyes = "happy"
        p.mouth = "o" if k < 0.62 else "smile"
        p.hands = ((-1, -3), (1, -3)) if 0.12 <= k < 0.62 else ((-1, 3), (1, 3))
        p.antenna = 18 + 25 * math.sin(k * 18) * (1 - k)
        p.blush = "big"
        for i, x0 in enumerate((10, 50)):
            y = 30 - k * 22 - i * 3
            if y > 2:
                p.fx.append(("heart", x0, y))
    elif name == "hungry":
        p.eyes = "look"
        p.look = (1, -1)
        p.mouth = "wavy"
        p.hands = ((-1, 3), ("front", -2, 9))
        p.antenna = 30
        p.light_level = wave(t, 1.2, 0.5, 0.9)
        p.fx = [("bubble", 46, 3), ("bowl", 46, 4)]
        if int(t * 2) % 2:     # a rumble by the tummy
            for i in range(3):
                p.fx.append(("px", 11 - i, 46 + (i % 2), (200, 195, 225)))
    elif name == "eating":
        chomp = int(t / 0.3) % 2
        p.eyes = "happy"
        p.mouth = "chomp"
        p.talk = chomp
        p.hands = (("front", -5, 7), ("front", 5, 7))
        p.fx = [("cookie", 29, 43 + (0 if chomp else 1))]
        crumb = (t % 0.6) / 0.6
        p.fx += [("px", 26 + int(crumb * 2), 49 + crumb * 8, (214, 150, 80)),
                 ("px", 36 - int(crumb * 2), 50 + crumb * 7, (214, 150, 80))]
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
            p.fx.append(("z", 50 + kk * 8, 30 - kk * 26))
    elif name == "sad":
        p.eyes = "worried"
        p.mouth = "frown"
        p.dy = 1
        p.antenna = 60
        p.light_level = 0.55
        k = t / length
        p.fx = [("drop", 23, 40 + int(k * 10))] if k < 0.7 else []
    elif name == "surprised":
        k = t / length
        p.dy = -4 * math.sin(min(1.0, k / 0.35) * math.pi) if k < 0.35 else 0
        p.squash = 0.92 if k < 0.35 else 1.0
        p.eyes = "wide"
        p.mouth = "o"
        p.antenna = 0
        p.antenna_len = 11
        p.hands = ((-1, -4), (1, -4))
        if int(t * 5) % 2 == 0:
            p.fx = [("excl", 12, 12)]
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
            p.fx.append(("spark", 32 + 12 * math.cos(a), 16 + 3 * math.sin(a), GOLD, 1))
    elif name == "celebrate":
        k = (t / length * 2) % 1
        p.dy = -5 * math.sin(k * math.pi)
        p.squash = 0.92 if 0.1 < k < 0.9 else 1.15
        p.feet = not (0.15 < k < 0.85)
        p.eyes = "happy"
        p.mouth = "o"
        p.hands = ((-1, -6), (1, -6))
        hue = (t / length) % 1
        rr, gg, bb = colorsys.hsv_to_rgb(hue, 0.6, 1)
        p.light = (round(rr * 255), round(gg * 255), round(bb * 255))
        rng = np.random.default_rng(7)
        for i in range(14):
            x = rng.integers(4, 60)
            y = (rng.integers(0, 64) + t * 30) % 64
            hh = rng.random()
            r2, g2, b2 = colorsys.hsv_to_rgb(hh, 0.7, 1)
            p.fx.append(("px", x, y, (round(r2 * 255), round(g2 * 255), round(b2 * 255))))
    elif name == "shy":
        s = math.sin(2 * math.pi * t / length)
        p.dx = round(s)
        p.eyes = "down"
        p.mouth = "w"
        p.blush = "big"
        p.hands = (("front", -11, 3), ("front", 11, 3))
        p.antenna = 30 + 10 * s
        if (t % length) > length * 0.6:
            p.fx = [("heart", 46, 10 - int((t % length - length * 0.6) * 8))]
    elif name == "low_battery":
        p.eyes = "half"
        p.mouth = "flat"
        p.squash = 1.08
        p.dy = 1
        p.antenna = 62
        p.light_level = 0.6 if int(t * 2) % 2 else 0.15
        if int(t * 2) % 2:
            p.fx = [("battery", 44, 10)]
    elif name == "charging":
        p.eyes = "happy"
        p.mouth = "smile"
        p.dy = bob
        p.light_level = wave(t, 0.8, 0.6, 1)
        p.fx = [("bolt", 47, 6 + int(wave(t, 0.8, 0, 2)))]
        for i in range(3):
            k = (t / length + i / 3) % 1
            p.fx.append(("px", 12 + i * 20, 52 - k * 36, (255, 230, 90)))
    return p


# ---------------------------------------------------------------- output
def frames(name, length, pal, scale):
    n = max(1, round(length * FPS))
    out = []
    for i in range(n):
        img = render(pose_for(name, i / FPS, length), pal)
        out.append(Image.fromarray(img).resize((N * scale, N * scale), Image.NEAREST))
    return out


def font(size):
    for path in ("/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc", "/System/Library/Fonts/PingFang.ttc"):
        try:
            return ImageFont.truetype(path, size)
        except OSError:
            pass
    return ImageFont.load_default()


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--color", default="ff9ec8", help="body colour, RRGGBB")
    ap.add_argument("--scale", type=int, default=4)
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    pal = ramp(args.color)

    all_frames = {}
    for name, length, _ in EXPRESSIONS:
        fr = frames(name, length, pal, args.scale)
        all_frames[name] = fr
        fr[0].save(args.out / f"{name}.gif", save_all=True, append_images=fr[1:], duration=1000 // FPS, loop=0)

    # grid of every expression, animated over the longest loop
    cols, cell, lab = 6, N * 3, 30
    rows = (len(EXPRESSIONS) + cols - 1) // cols
    f_title = font(20)
    total = max(len(f) for f in all_frames.values())
    grid_frames = []
    for i in range(total):
        sheet = Image.new("RGB", (cols * (cell + 8) + 8, rows * (cell + lab + 8) + 8), (22, 20, 30))
        d = ImageDraw.Draw(sheet)
        for k, (name, _, zh) in enumerate(EXPRESSIONS):
            fr = all_frames[name]
            im = fr[i % len(fr)].resize((cell, cell), Image.NEAREST)
            x, y = 8 + (k % cols) * (cell + 8), 8 + (k // cols) * (cell + lab + 8)
            d.text((x + 2, y), f"{zh} {name}", fill=(220, 215, 240), font=f_title)
            sheet.paste(im, (x, y + lab))
        grid_frames.append(sheet)
    grid_frames[0].save(args.out / "all.gif", save_all=True, append_images=grid_frames[1:],
                        duration=1000 // FPS, loop=0)
    still = grid_frames[0].copy()
    d = ImageDraw.Draw(still)
    for k, (name, length, zh) in enumerate(EXPRESSIONS):   # a telling frame of each
        fr = all_frames[name]
        pick = {"boot": 0.8, "happy": 0.3, "off": 0.3, "surprised": 0.15, "celebrate": 0.25}.get(name, 0.4)
        im = fr[int(len(fr) * pick)].resize((cell, cell), Image.NEAREST)
        x, y = 8 + (k % cols) * (cell + 8), 8 + (k // cols) * (cell + lab + 8)
        still.paste(im, (x, y + lab))
    still.save(args.out / "sheet.png")


if __name__ == "__main__":
    main()

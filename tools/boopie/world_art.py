#!/usr/bin/env python3
# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0

"""Draws the pet's world (docs/boopie-world.md) and writes it as C.

  python3 tools/boopie/world_art.py            # writes esp32/components/boopie/world/boopie_world_art.[ch]
  python3 tools/boopie/world_art.py --png DIR  # and a contact sheet, to look at

The style is the soft GBA monster games': flat colours, two or three tones,
and every object outlined in a darker shade of itself (done here, after
drawing). Scenes are 156 x 156 (the screen at 3x); each room's background
(walls, floor) is one image, and what stands in it is a sprite, so the
board can show each piece only from the pet's level that brings it. All of
it shares one palette of up to 255 colours (index 0 is see-through), a byte
a pixel.
"""

from __future__ import annotations

import argparse
import math
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "esp32" / "components" / "boopie" / "world"
W = 156


def clamp(v: float) -> int:
    return max(0, min(255, int(v)))


def dark(c):
    return (clamp(c[0] * 0.42), clamp(c[1] * 0.42), clamp(c[2] * 0.5 + 10))


def sh(c, k):
    return tuple(clamp(v * k) for v in c[:3])


class Layer:
    """A sprite being drawn: flat colours on a clear canvas, then outlined."""

    def __init__(self, size: int = 64):
        self.size = size
        self.im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        self.px = self.im.load()
        self.d = ImageDraw.Draw(self.im)

    def put(self, x, y, c):
        x, y = int(x), int(y)
        if 0 <= x < self.size and 0 <= y < self.size:
            self.px[x, y] = tuple(c[:3]) + (255,)

    def rect(self, x0, y0, x1, y1, c):
        self.d.rectangle([x0, y0, x1, y1], fill=tuple(c) + (255,))

    def ell(self, x0, y0, x1, y1, c):
        self.d.ellipse([x0, y0, x1, y1], fill=tuple(c) + (255,))

    def poly(self, pts, c):
        self.d.polygon(pts, fill=tuple(c) + (255,))

    def outline(self):
        src = self.im.copy()
        s = src.load()
        n = self.size
        for y in range(n):
            for x in range(n):
                if s[x, y][3]:
                    continue
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < n and 0 <= ny < n and s[nx, ny][3]:
                        self.px[x, y] = dark(s[nx, ny]) + (255,)
                        break


# ---------------------------------------------------------------- furniture
# Each draws with (x, y) its foot: where it stands on the floor (or, on a wall,
# its bottom middle). The sprite's anchor is that point.

def tv_old(L, x, y):
    L.rect(x - 11, y - 8, x + 9, y, (176, 128, 88))
    L.rect(x - 11, y - 8, x + 9, y - 7, (200, 152, 108))
    L.rect(x - 10, y - 24, x + 8, y - 9, (72, 72, 88))
    L.rect(x - 8, y - 22, x + 6, y - 11, (120, 184, 232))
    L.rect(x - 7, y - 21, x - 2, y - 18, (184, 224, 248))
    L.rect(x + 12, y - 6, x + 19, y - 2, (200, 200, 216))
    L.put(x + 14, y - 4, (232, 72, 72)); L.put(x + 17, y - 5, (72, 120, 232))


def tv_flat(L, x, y):
    L.rect(x - 13, y - 7, x + 11, y, (120, 88, 64))
    L.rect(x - 13, y - 7, x + 11, y - 6, (148, 112, 84))
    L.rect(x - 14, y - 28, x + 12, y - 11, (40, 40, 52))
    L.rect(x - 12, y - 26, x + 10, y - 13, (104, 200, 232))
    L.rect(x - 11, y - 25, x - 4, y - 22, (184, 236, 248))
    L.rect(x - 2, y - 10, x, y - 8, (40, 40, 52))
    L.rect(x + 14, y - 6, x + 21, y - 2, (200, 200, 216))
    L.put(x + 16, y - 4, (232, 72, 72)); L.put(x + 19, y - 5, (72, 120, 232))


def bookshelf(L, x, y):
    L.rect(x - 8, y - 26, x + 8, y, (176, 120, 80))
    for k in range(3):
        sy = y - 24 + k * 8
        L.rect(x - 7, sy, x + 7, sy + 6, (120, 80, 56))
        for b in range(5):
            col = [(232, 96, 96), (96, 152, 232), (248, 208, 88), (120, 192, 120), (200, 140, 220)][(b + k) % 5]
            L.rect(x - 6 + b * 3, sy + 1 + (b % 2), x - 5 + b * 3, sy + 6, col)


def radio(L, x, y):
    L.rect(x - 6, y - 7, x + 6, y, (120, 176, 160))
    L.rect(x - 5, y - 6, x, y - 1, (72, 96, 96))
    L.rect(x + 2, y - 5, x + 4, y - 4, (248, 216, 96))
    L.rect(x + 3, y - 11, x + 3, y - 7, (88, 88, 96))


def stairs_up(L, x, y):
    for k in range(5):
        c = (200, 152, 104) if k % 2 else (176, 128, 88)
        L.rect(x - 8, y - 20 + k * 4, x + 8, y - 17 + k * 4, c)
    L.rect(x - 9, y - 20, x - 9, y - 1, (148, 100, 64))
    L.rect(x + 9, y - 20, x + 9, y - 1, (148, 100, 64))


def stairs_down(L, x, y):
    L.rect(x - 9, y - 14, x + 9, y, (120, 84, 56))
    for k in range(4):
        c = (176, 128, 88) if k % 2 else (152, 108, 72)
        L.rect(x - 8, y - 13 + k * 3, x + 8 - k * 2, y - 11 + k * 3, c)
    L.rect(x - 9, y - 14, x + 9, y - 14, (200, 152, 104))


def table(L, x, y):
    L.rect(x - 1, y - 4, x + 1, y, (150, 104, 66))
    L.ell(x - 12, y - 16, x + 12, y - 4, (200, 144, 96))
    L.ell(x - 11, y - 16, x + 9, y - 7, (224, 168, 116))
    L.ell(x - 4, y - 14, x + 2, y - 10, (255, 255, 255))
    L.rect(x - 2, y - 15, x, y - 13, (248, 168, 184))


def plant(L, x, y):
    L.rect(x - 3, y - 5, x + 3, y, (216, 120, 80))
    for dx, dy in ((-4, -8), (3, -9), (0, -12), (-2, -10), (2, -6)):
        L.ell(x + dx - 2, y + dy - 1, x + dx + 2, y + dy + 1, (104, 184, 96))


def plant_big(L, x, y):
    L.rect(x - 4, y - 7, x + 4, y, (200, 112, 80))
    L.rect(x - 5, y - 8, x + 5, y - 7, (224, 136, 96))
    for dx, dy, r in ((-5, -14, 4), (5, -15, 4), (0, -21, 5), (-3, -18, 3), (4, -10, 3), (-5, -10, 3)):
        L.ell(x + dx - r, y + dy - r * 0.6, x + dx + r, y + dy + r * 0.6, (96, 176, 96))
        L.put(x + dx - 1, y + dy - 1, (144, 208, 120))


def bowl_full(L, x, y):
    L.ell(x - 5, y - 5, x + 5, y, (232, 96, 96))
    L.ell(x - 4, y - 5, x + 4, y - 2, (208, 160, 96))
    L.put(x - 1, y - 5, (232, 196, 128)); L.put(x + 2, y - 4, (176, 120, 72))


def bowl_empty(L, x, y):
    L.ell(x - 5, y - 5, x + 5, y, (232, 96, 96))
    L.ell(x - 4, y - 5, x + 4, y - 2, (160, 72, 72))


def bed(L, x, y):
    L.rect(x - 10, y - 30, x + 10, y, (176, 128, 88))
    L.rect(x - 9, y - 29, x + 9, y - 2, (248, 248, 248))
    L.rect(x - 7, y - 28, x + 7, y - 22, (255, 255, 255))
    L.rect(x - 7, y - 22, x + 7, y - 22, (216, 216, 224))
    L.rect(x - 9, y - 19, x + 9, y - 2, (104, 152, 232))
    L.rect(x - 9, y - 19, x + 9, y - 18, (144, 184, 248))
    for k in range(3):
        L.rect(x - 7, y - 14 + k * 4, x + 7, y - 14 + k * 4, (88, 128, 208))


def bed_big(L, x, y):
    L.rect(x - 15, y - 36, x + 15, y, (160, 108, 72))
    L.rect(x - 15, y - 40, x + 15, y - 35, (184, 128, 88))
    L.rect(x - 13, y - 34, x + 13, y - 2, (248, 248, 248))
    L.rect(x - 11, y - 33, x - 2, y - 27, (255, 255, 255)); L.rect(x + 2, y - 33, x + 11, y - 27, (255, 255, 255))
    L.rect(x - 13, y - 24, x + 13, y - 2, (232, 128, 152))
    L.rect(x - 13, y - 24, x + 13, y - 23, (248, 168, 184))
    for k in range(4):
        for j in range(5):
            L.put(x - 10 + j * 5, y - 20 + k * 4, (255, 216, 224))


def wardrobe(L, x, y):
    L.rect(x - 10, y - 26, x + 10, y, (192, 136, 88))
    L.rect(x - 10, y - 26, x + 10, y - 24, (216, 160, 108))
    L.rect(x, y - 24, x, y, (148, 100, 64))
    L.rect(x - 3, y - 14, x - 2, y - 11, (248, 216, 96)); L.rect(x + 2, y - 14, x + 3, y - 11, (248, 216, 96))


def mirror(L, x, y):
    L.ell(x - 5, y - 13, x + 5, y - 1, (200, 160, 104))
    L.ell(x - 4, y - 12, x + 4, y - 2, (184, 216, 240))
    L.rect(x - 2, y - 10, x - 1, y - 7, (232, 244, 255))


def desk(L, x, y):
    L.rect(x - 11, y - 10, x + 11, y, (192, 136, 88))
    L.rect(x - 11, y - 10, x + 11, y - 8, (216, 160, 108))
    L.rect(x - 8, y - 18, x + 2, y - 11, (88, 88, 104))
    L.rect(x - 7, y - 17, x + 1, y - 12, (136, 216, 160))
    L.rect(x + 4, y - 13, x + 9, y - 11, (232, 96, 96))


def picture(L, x, y):
    L.rect(x - 8, y - 12, x + 8, y, (176, 120, 80))
    L.rect(x - 6, y - 10, x + 6, y - 2, (184, 216, 248))
    L.ell(x - 6, y - 6, x + 2, y - 1, (120, 192, 120))
    L.ell(x + 1, y - 9, x + 4, y - 6, (255, 216, 96))


def poster(L, x, y):
    L.rect(x - 6, y - 16, x + 6, y, (248, 240, 216))
    L.rect(x - 5, y - 15, x + 5, y - 6, (40, 48, 96))
    for sx, sy in ((-3, -13), (2, -12), (0, -9), (3, -14)):
        L.put(x + sx, y + sy, (255, 240, 160))
    L.rect(x - 4, y - 4, x + 4, y - 3, (232, 96, 120))


def sofa(L, x, y):
    L.rect(x - 16, y - 14, x + 16, y, (120, 160, 216))
    L.rect(x - 16, y - 18, x + 16, y - 12, (104, 144, 200))
    L.rect(x - 18, y - 14, x - 14, y, (96, 132, 188)); L.rect(x + 14, y - 14, x + 18, y, (96, 132, 188))
    L.rect(x - 13, y - 11, x - 1, y - 6, (144, 184, 232)); L.rect(x + 1, y - 11, x + 13, y - 6, (144, 184, 232))
    L.rect(x - 9, y - 16, x - 3, y - 11, (248, 200, 104))


def lamp(L, x, y):
    L.rect(x - 4, y - 2, x + 4, y, (88, 80, 72))
    L.rect(x, y - 26, x, y - 2, (96, 88, 80))
    L.ell(x - 6, y - 34, x + 6, y - 24, (255, 230, 170))
    L.ell(x - 5, y - 34, x + 2, y - 28, (255, 248, 216))


def aquarium(L, x, y):
    L.rect(x - 12, y - 6, x + 12, y, (120, 88, 64))
    L.rect(x - 11, y - 22, x + 11, y - 7, (120, 200, 240))
    L.rect(x - 11, y - 22, x + 11, y - 20, (184, 232, 248))
    L.rect(x - 11, y - 9, x + 11, y - 7, (232, 216, 160))
    L.ell(x - 7, y - 16, x - 2, y - 13, (248, 152, 72)); L.put(x - 8, y - 15, (248, 152, 72))
    L.ell(x + 3, y - 13, x + 7, y - 10, (248, 232, 104))
    for k in range(3):
        L.rect(x + 8 - k, y - 15 + k * 2, x + 8 - k, y - 10, (96, 184, 104))


def trophy(L, x, y):
    L.rect(x - 9, y - 4, x + 9, y, (148, 104, 66))
    L.rect(x - 2, y - 8, x + 2, y - 4, (232, 184, 64))
    L.ell(x - 6, y - 18, x + 6, y - 8, (248, 208, 72))
    L.ell(x - 4, y - 17, x, y - 12, (255, 240, 160))
    L.rect(x - 8, y - 16, x - 6, y - 12, (232, 184, 64)); L.rect(x + 6, y - 16, x + 8, y - 12, (232, 184, 64))


def star_lamp(L, x, y):
    L.rect(x - 3, y - 2, x + 3, y, (120, 104, 168))
    L.rect(x, y - 8, x, y - 2, (120, 104, 168))
    pts = []
    for k in range(10):
        a = -math.pi / 2 + k * math.pi / 5
        r = 6 if k % 2 == 0 else 2.6
        pts.append((x + r * math.cos(a), y - 14 + r * math.sin(a)))
    L.poly(pts, (255, 224, 104))


def beanbag(L, x, y):
    L.ell(x - 10, y - 12, x + 10, y, (232, 120, 120))
    L.ell(x - 8, y - 12, x + 2, y - 6, (248, 168, 160))
    L.ell(x - 4, y - 7, x + 6, y - 2, (208, 96, 104))


def telescope(L, x, y):
    for dx in (-5, 0, 5):
        L.rect(x + dx // 2, y - 10, x + dx // 2, y, (120, 88, 64))
        L.put(x + dx, y, (120, 88, 64))
    L.poly([(x - 3, y - 12), (x + 9, y - 22), (x + 11, y - 19), (x - 1, y - 9)], (96, 120, 184))
    L.rect(x + 8, y - 23, x + 11, y - 18, (232, 200, 96))


def window(L, x, y, night):
    L.rect(x - 13, y - 18, x + 13, y, (176, 128, 88))
    sky = ((184, 216, 248), (152, 196, 240)) if not night else ((48, 56, 112), (72, 80, 136))
    L.rect(x - 12, y - 17, x + 12, y - 1, sky[0])
    L.rect(x - 12, y - 9, x + 12, y - 1, sky[1])
    if night:
        for sx, sy in ((-9, -14), (5, -15), (-2, -6), (9, -5)):
            L.put(x + sx, y + sy, (240, 240, 255))
        L.ell(x + 5, y - 9, x + 9, y - 5, (248, 240, 200))
    else:
        for k in range(5):
            L.put(x - 10 + k, y - 15, (255, 255, 255)); L.put(x - 9 + k, y - 14, (255, 255, 255))
    L.rect(x - 13, y - 9, x + 13, y - 9, (176, 128, 88))
    L.rect(x, y - 18, x, y, (176, 128, 88))


def window_day(L, x, y):
    window(L, x, y, False)


def window_night(L, x, y):
    window(L, x, y, True)


def door_out(L, x, y):
    """The way out: a mat on the floor (no outline drawn round floor marks, see FLAT)."""
    for yy in range(y - 6, y):
        for xx in range(x - 10, x + 10):
            L.put(xx, yy, (200, 96, 80) if (xx + yy) % 3 else (176, 80, 64))


def rug_small(L, x, y):
    rug(L, x, y, 22, 13, (216, 96, 88), (248, 200, 104))


def rug_big(L, x, y):
    rug(L, x, y, 30, 17, (120, 96, 176), (248, 216, 128))


def rug_round(L, x, y):
    for yy in range(y - 26, y + 1):
        for xx in range(x - 22, x + 23):
            d = ((xx - x) / 22) ** 2 + ((yy - y + 13) / 13) ** 2
            if d <= 1:
                L.put(xx, yy, (120, 168, 232) if d < 0.45 or d > 0.75 else (248, 248, 248))


def rug(L, x, y, rx, ry, a, b):
    cy = y - ry
    for yy in range(y - 2 * ry, y + 1):
        for xx in range(x - rx, x + rx + 1):
            d = max(abs(xx - x) / rx, abs(yy - cy) / ry)
            if d <= 1:
                L.put(xx, yy, a if d < 0.65 or d > 0.85 else b)


# ---------------------------------------------------------------- hints

HINT_INK = (88, 104, 168)


def hint(L, x, y, kind):
    """A white bubble with what tapping does in it; (x, y) its tail's tip."""
    y -= 7
    L.ell(x - 6, y - 6, x + 6, y + 5, (255, 255, 255))
    L.poly([(x - 2, y + 4), (x + 2, y + 4), (x, y + 7)], (255, 255, 255))
    c = HINT_INK
    if kind == "zzz":
        for zx, zy in ((-3, -2), (1, -4)):
            L.rect(x + zx, y + zy, x + zx + 2, y + zy, c); L.put(x + zx + 1, y + zy + 1, c)
            L.rect(x + zx, y + zy + 2, x + zx + 2, y + zy + 2, c)
    elif kind == "shirt":
        L.rect(x - 2, y - 3, x + 2, y + 2, (232, 96, 120)); L.rect(x - 4, y - 3, x + 4, y - 1, (232, 96, 120))
    elif kind == "game":
        L.rect(x - 4, y - 2, x + 4, y + 2, c); L.put(x - 2, y, (255, 255, 255)); L.put(x + 2, y - 1, (232, 96, 96))
    elif kind == "pen":
        for k in range(5):
            L.put(x - 2 + k, y + 2 - k, (232, 168, 64))
        L.put(x - 3, y + 3, c)
    elif kind == "note":
        L.rect(x, y - 4, x, y + 1, c); L.rect(x - 2, y + 1, x, y + 2, c); L.rect(x, y - 4, x + 2, y - 3, c)
    elif kind == "book":
        L.rect(x - 3, y - 3, x + 3, y + 2, (96, 152, 232)); L.rect(x, y - 3, x, y + 2, (255, 255, 255))
    elif kind == "food":
        L.ell(x - 3, y - 2, x + 3, y + 2, (232, 120, 72)); L.put(x, y - 3, (96, 168, 72))
    elif kind == "up":
        L.poly([(x, y - 4), (x - 3, y), (x + 3, y)], c); L.rect(x - 1, y, x + 1, y + 2, c)
    elif kind == "down":
        L.poly([(x, y + 3), (x - 3, y - 1), (x + 3, y - 1)], c); L.rect(x - 1, y - 3, x + 1, y - 1, c)
    elif kind == "door":
        L.rect(x - 2, y - 3, x + 2, y + 2, (176, 120, 80)); L.put(x + 1, y, (248, 216, 96))
    elif kind == "info":
        L.rect(x, y - 1, x, y + 2, c); L.put(x, y - 3, c)
    elif kind == "bang":
        L.rect(x, y - 3, x, y, (232, 72, 72)); L.put(x, y + 2, (232, 72, 72))


def hint_fn(kind):
    return lambda L, x, y: hint(L, x, y, kind)


# ---------------------------------------------------------------- rooms

FLOOR = (232, 204, 152)
FLOOR_L = (240, 216, 168)
FLOOR_D = (208, 176, 124)
WALL_H = 46


def room(paper_a, paper_b, wainscot=None, pattern=None):
    im = Image.new("RGB", (W, W))
    px = im.load()
    for y in range(W):
        for x in range(W):
            if y < WALL_H:
                c = paper_a if (x // 4) % 2 else paper_b
                if pattern and pattern(x, y):
                    c = sh(paper_b, 0.9)
                if wainscot and y >= WALL_H - 14:
                    c = wainscot if x % 6 else sh(wainscot, 0.85)
            else:
                row = (y - WALL_H) // 5
                c = FLOOR_L if row % 2 else FLOOR
                if (y - WALL_H) % 5 == 4 or (x + row * 9) % 26 == 0:
                    c = FLOOR_D
            px[x, y] = c
    for x in range(W):
        px[x, WALL_H - 3] = (200, 168, 120)
        px[x, WALL_H - 2] = (176, 140, 96)
        px[x, WALL_H - 1] = (150, 116, 80)
    return im


def bg_down():
    return room((248, 240, 216), (236, 224, 196))


def bg_down_fancy():
    return room((228, 240, 216), (212, 228, 200), wainscot=(176, 128, 88),
                pattern=lambda x, y: (x + y) % 8 == 0 or (x - y) % 8 == 0)


def bg_up():
    return room((232, 240, 248), (220, 232, 244))


def bg_up_stars():
    return room((88, 104, 168), (80, 96, 160),
                pattern=lambda x, y: (x * 7 + y * 13) % 41 == 0)


# ---------------------------------------------------------------- the list

# name, drawing, flat (a floor mark: no outline, drawn under everything)
SPRITES = [
    ("tv_old", tv_old, False), ("tv_flat", tv_flat, False), ("bookshelf", bookshelf, False),
    ("radio", radio, False), ("stairs_up", stairs_up, False), ("stairs_down", stairs_down, False),
    ("table", table, False), ("plant", plant, False), ("plant_big", plant_big, False),
    ("bowl_full", bowl_full, False), ("bowl_empty", bowl_empty, False), ("bed", bed, False),
    ("bed_big", bed_big, False), ("wardrobe", wardrobe, False), ("mirror", mirror, False), ("desk", desk, False),
    ("picture", picture, False), ("poster", poster, False), ("sofa", sofa, False), ("lamp", lamp, False),
    ("aquarium", aquarium, False), ("trophy", trophy, False), ("star_lamp", star_lamp, False),
    ("beanbag", beanbag, False), ("telescope", telescope, False),
    ("window_day", window_day, False), ("window_night", window_night, False),
    ("door_out", door_out, True), ("rug_small", rug_small, True), ("rug_big", rug_big, True),
    ("rug_round", rug_round, True),
]
HINTS = ["zzz", "shirt", "game", "pen", "note", "book", "food", "up", "down", "door", "info", "bang"]
BACKGROUNDS = [("bg_down", bg_down), ("bg_down_fancy", bg_down_fancy), ("bg_up", bg_up), ("bg_up_stars", bg_up_stars)]


def draw_sprite(fn, flat):
    L = Layer(80)
    fn(L, 40, 60)
    if not flat:
        L.outline()
    box = L.im.getbbox()
    crop = L.im.crop(box)
    return crop, 40 - box[0], 60 - box[1]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--png", type=Path, help="also write a contact sheet here")
    args = ap.parse_args()

    images = []   # (name, RGBA image, ax, ay, flat)
    for name, fn in BACKGROUNDS:
        images.append((name, fn().convert("RGBA"), 0, 0, True))
    for name, fn, flat in SPRITES:
        im, ax, ay = draw_sprite(fn, flat)
        images.append((name, im, ax, ay, flat))
    for kind in HINTS:
        im, ax, ay = draw_sprite(hint_fn(kind), False)
        images.append((f"hint_{kind}", im, ax, ay, False))

    palette = {}
    for _, im, *_ in images:
        for r, g, b, a in im.convert('RGBA').get_flattened_data() if hasattr(im, 'get_flattened_data') else im.getdata():
            if a and (r, g, b) not in palette:
                palette[(r, g, b)] = len(palette) + 1
    if len(palette) > 255:
        raise SystemExit(f"{len(palette)} colours: more than a byte holds")

    h = ['/*', ' * Copyright (c) 2026 Boopie contributors', ' * SPDX-License-Identifier: Apache-2.0', ' *',
         ' * Generated by tools/boopie/world_art.py: do not edit.', ' */', '', '#pragma once', '',
         '#include <stdint.h>', '', '/* A picture: indexes into boopie_art_palette (0: see-through), row by row;',
         ' * (ax, ay) where it stands, from its top left. */',
         'typedef struct {', '    uint16_t w, h;', '    int16_t ax, ay;', '    const uint8_t *px;', '} boopie_art_t;', '',
         'typedef enum {']
    for name, *_ in images:
        h.append(f'    BOOPIE_ART_{name.upper()},')
    h += ['    BOOPIE_ART_COUNT,', '} boopie_art_id_t;', '',
          f'#define BOOPIE_ART_COLOURS {len(palette) + 1}',
          'extern const uint32_t boopie_art_palette[BOOPIE_ART_COLOURS];   /* 0xRRGGBB */',
          'extern const boopie_art_t boopie_art[BOOPIE_ART_COUNT];', '']
    c = ['/*', ' * Copyright (c) 2026 Boopie contributors', ' * SPDX-License-Identifier: Apache-2.0', ' *',
         ' * Generated by tools/boopie/world_art.py: do not edit.', ' */', '',
         '#include "boopie_world_art.h"', '', 'const uint32_t boopie_art_palette[BOOPIE_ART_COLOURS] = {', '    0x000000,']
    for (r, g, b), i in sorted(palette.items(), key=lambda kv: kv[1]):
        c.append(f'    0x{r:02x}{g:02x}{b:02x},')
    c.append('};')
    total = 0
    for name, im, ax, ay, _ in images:
        data = bytes(palette[(r, g, b)] if a else 0 for r, g, b, a in (im.get_flattened_data() if hasattr(im, 'get_flattened_data') else im.getdata()))
        total += len(data)
        c.append(f'\nstatic const uint8_t PX_{name.upper()}[{len(data)}] = {{')
        for k in range(0, len(data), 24):
            c.append('    ' + ', '.join(str(v) for v in data[k:k + 24]) + ',')
        c.append('};')
    c.append('\nconst boopie_art_t boopie_art[BOOPIE_ART_COUNT] = {')
    for name, im, ax, ay, _ in images:
        c.append(f'    [BOOPIE_ART_{name.upper()}] = {{ {im.width}, {im.height}, {ax}, {ay}, PX_{name.upper()} }},')
    c.append('};')
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "boopie_world_art.h").write_text("\n".join(h) + "\n")
    (OUT / "boopie_world_art.c").write_text("\n".join(c) + "\n")
    print(f"{len(images)} pictures, {len(palette)} colours, {total} bytes")

    if args.png:
        args.png.mkdir(parents=True, exist_ok=True)
        sheet = Image.new("RGBA", (W * 4 + 30, W + 10 + 70 * 6), (40, 44, 56, 255))
        x = y = 0
        for i, (name, im, *_) in enumerate(images):
            if i < len(BACKGROUNDS):
                sheet.paste(im, (i * (W + 10), 0))
                continue
            if x + im.width > sheet.width:
                x, y = 0, y + 52
            sheet.paste(im, (x, W + 10 + y), im)
            x += im.width + 6
        sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST).save(args.png / "world-art.png")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

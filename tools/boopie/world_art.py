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



# ---------------------------------------------------------------- outdoors

GRASS = (172, 206, 128)
GRASS_D = (140, 180, 102)
GRASS_L = (196, 224, 156)
SAND = (226, 210, 160)
OUT_W = 360   # the outdoor scene: about two and a third screens wide


def bg_outside():
    import random
    r = random.Random(3)
    im = Image.new("RGB", (OUT_W, W), GRASS)
    px = im.load()

    def sand(x, y):
        if 0 <= x < OUT_W and 0 <= y < W:
            px[x, y] = SAND

    # The path: from the door down, then east to the woods; its edges dithered.
    def band(x0, x1, y0, y1):
        for y in range(y0 - 1, y1 + 2):
            for x in range(x0 - 1, x1 + 2):
                edge = x in (x0 - 1, x1 + 1) or y in (y0 - 1, y1 + 1)
                if not edge or (x + y) % 2:
                    sand(x, y)
    band(62, 78, 64, 100)
    band(62, OUT_W - 1, 92, 104)
    for _ in range(140):   # tufts
        x, y = r.randrange(2, OUT_W - 3), r.randrange(4, W - 3)
        if px[x, y] == GRASS:
            px[x, y] = GRASS_D; px[x + 2, y] = GRASS_D; px[x + 1, y + 1] = GRASS_D
    for _ in range(80):
        x, y = r.randrange(OUT_W), r.randrange(W)
        if px[x, y] == GRASS:
            px[x, y] = GRASS_L
    for _ in range(26):   # flowers
        x, y = r.randrange(4, OUT_W - 4), r.randrange(4, W - 4)
        if px[x, y] == GRASS:
            c = r.choice(((255, 255, 255), (248, 120, 120), (255, 216, 96)))
            for dx, dy in ((0, -1), (-1, 0), (1, 0), (0, 1)):
                if px[x + dx, y + dy] == GRASS:
                    px[x + dx, y + dy] = c
            px[x, y] = (248, 200, 64)
    return im


def house_front(L, x, y):
    L.rect(x - 28, y - 30, x + 28, y, (248, 236, 208))
    L.rect(x - 28, y - 4, x + 28, y, (216, 200, 168))
    L.poly([(x - 33, y - 28), (x, y - 46), (x + 33, y - 28)], (216, 88, 72))
    L.rect(x - 33, y - 30, x + 33, y - 26, (216, 88, 72))
    for k in range(4):
        L.rect(x - 32 + k * 3, y - 31 - k * 3, x + 32 - k * 3, y - 31 - k * 3, (240, 120, 96))
    L.rect(x - 33, y - 26, x + 33, y - 25, (168, 64, 56))
    L.rect(x + 12, y - 52, x + 18, y - 38, (176, 160, 150))
    L.rect(x - 6, y - 18, x + 6, y, (168, 112, 72))
    L.rect(x - 5, y - 17, x + 5, y - 11, (196, 140, 92))
    L.put(x + 3, y - 8, (248, 216, 96))
    for wx in (x - 22, x + 11):
        L.rect(wx, y - 20, wx + 10, y - 11, (120, 176, 232))
        L.rect(wx, y - 20, wx + 4, y - 16, (184, 224, 248))
        L.rect(wx + 5, y - 20, wx + 5, y - 11, (248, 236, 208))


def mailbox(L, x, y):
    L.rect(x - 1, y - 7, x, y, (160, 112, 72))
    L.rect(x - 5, y - 13, x + 4, y - 7, (232, 88, 88))
    L.rect(x - 5, y - 13, x + 4, y - 12, (248, 140, 140))
    L.rect(x + 5, y - 13, x + 5, y - 10, (248, 216, 96))


def sign(L, x, y):
    L.rect(x - 1, y - 7, x, y, (160, 112, 72))
    L.rect(x - 8, y - 14, x + 8, y - 6, (224, 184, 128))
    for k in range(9):
        L.put(x - 5 + k, y - 10, (120, 80, 48))
    for k in range(3):
        L.put(x + 3 - k, y - 10 - k, (120, 80, 48)); L.put(x + 3 - k, y - 10 + k, (120, 80, 48))


def tree(L, x, y):
    L.rect(x - 2, y - 8, x + 2, y, (148, 104, 64))
    L.rect(x + 1, y - 8, x + 2, y, (116, 80, 48))
    L.ell(x - 12, y - 30, x + 12, y - 6, (96, 168, 88))
    for k in range(-2, 3):
        L.ell(x + k * 5 - 4, y - 10, x + k * 5 + 4, y - 4, (72, 140, 72))
    L.ell(x - 9, y - 28, x + 2, y - 18, (136, 200, 104))


def fruit_tree(L, x, y):
    tree(L, x, y)
    for fx, fy in ((-6, -20), (4, -24), (6, -14), (-3, -12)):
        L.ell(x + fx - 1, y + fy - 1, x + fx + 1, y + fy + 1, (248, 144, 56))


def pine(L, x, y):
    L.rect(x - 1, y - 4, x + 1, y, (140, 96, 60))
    for k, w in enumerate((9, 7, 5)):
        top = y - 8 - k * 6
        L.poly([(x, top - 7), (x - w, top + 4), (x + w, top + 4)], (88, 152, 88))
        L.poly([(x, top - 7), (x - w, top + 4), (x - 1, top + 4)], (120, 184, 104))


def fence_h(L, x, y):
    L.rect(x - 16, y - 8, x + 16, y - 7, (208, 168, 112))
    L.rect(x - 16, y - 4, x + 16, y - 3, (208, 168, 112))
    for px_ in range(x - 16, x + 17, 8):
        L.rect(px_, y - 11, px_ + 2, y, (224, 184, 128))
        L.rect(px_ + 2, y - 11, px_ + 2, y, (184, 144, 96))


def lamp_post(L, x, y):
    L.rect(x - 1, y - 24, x + 1, y, (80, 80, 96))
    L.rect(x - 3, y - 1, x + 3, y, (80, 80, 96))
    L.rect(x - 4, y - 30, x + 4, y - 24, (80, 80, 96))
    L.rect(x - 3, y - 29, x + 3, y - 25, (255, 232, 160))


def bench(L, x, y):
    L.rect(x - 12, y - 9, x + 12, y - 7, (200, 152, 100))
    L.rect(x - 12, y - 14, x + 12, y - 12, (200, 152, 100))
    for lx in (x - 10, x + 9):
        L.rect(lx, y - 7, lx + 1, y, (120, 88, 64))


def flowerbed(L, x, y):
    L.rect(x - 14, y - 5, x + 14, y, (176, 124, 84))
    for k in range(7):
        c = [(248, 120, 140), (255, 216, 96), (255, 255, 255), (200, 150, 240)][k % 4]
        fx = x - 12 + k * 4
        L.rect(fx, y - 8, fx, y - 5, (96, 168, 72))
        L.ell(fx - 1, y - 11, fx + 1, y - 8, c)


def well(L, x, y):
    L.ell(x - 9, y - 9, x + 9, y, (176, 168, 156))
    L.ell(x - 6, y - 8, x + 6, y - 3, (80, 120, 184))
    L.rect(x - 8, y - 22, x - 7, y - 5, (160, 112, 72)); L.rect(x + 7, y - 22, x + 8, y - 5, (160, 112, 72))
    L.poly([(x - 11, y - 21), (x, y - 27), (x + 11, y - 21)], (216, 88, 72))


def scarecrow(L, x, y):
    L.rect(x, y - 16, x + 1, y, (160, 112, 72))
    L.rect(x - 6, y - 12, x + 7, y - 11, (160, 112, 72))
    L.rect(x - 3, y - 13, x + 4, y - 6, (120, 160, 224))
    L.ell(x - 3, y - 21, x + 4, y - 14, (240, 224, 184))
    L.rect(x - 5, y - 22, x + 6, y - 21, (232, 192, 96)); L.rect(x - 2, y - 25, x + 3, y - 22, (240, 200, 104))


def coop(L, x, y):
    L.rect(x - 14, y - 16, x + 14, y, (232, 196, 140))
    for k in range(x - 14, x + 15, 4):
        L.rect(k, y - 16, k, y, (200, 160, 110))
    L.poly([(x - 17, y - 15), (x, y - 26), (x + 17, y - 15)], (200, 88, 72))
    L.rect(x - 4, y - 9, x + 4, y, (120, 84, 56))


def chicken(L, x, y):
    L.ell(x - 3, y - 6, x + 3, y, (248, 248, 240))
    L.put(x + 2, y - 7, (232, 72, 72)); L.put(x + 3, y - 7, (232, 72, 72))
    L.put(x + 4, y - 4, (248, 192, 64)); L.put(x + 2, y - 5, (40, 40, 40))


def beehive(L, x, y):
    for k in range(3):
        L.rect(x - 6, y - 5 - k * 5, x + 6, y - 1 - k * 5, (250, 225, 140) if k % 2 else (240, 210, 120))
    L.rect(x - 7, y - 17, x + 7, y - 15, (200, 150, 90))
    L.rect(x - 1, y - 3, x + 1, y - 2, (90, 60, 30))


def barn(L, x, y):
    L.rect(x - 24, y - 30, x + 24, y, (200, 72, 60))
    for k in range(x - 24, x + 25, 5):
        L.rect(k, y - 30, k, y, (168, 56, 48))
    L.poly([(x - 28, y - 28), (x, y - 46), (x + 28, y - 28)], (150, 60, 52))
    L.rect(x - 9, y - 18, x + 9, y, (236, 230, 220))
    L.rect(x - 7, y - 16, x + 7, y, (184, 64, 52))
    for k in range(15):
        L.put(x - 7 + k, y - 16 + k, (236, 230, 220)); L.put(x + 7 - k, y - 16 + k, (236, 230, 220))
    L.rect(x - 4, y - 28, x + 4, y - 22, (60, 40, 30)); L.rect(x - 3, y - 27, x + 3, y - 23, (240, 210, 140))


def rock(L, x, y):
    L.ell(x - 7, y - 8, x + 7, y, (168, 156, 140))
    L.ell(x - 6, y - 8, x + 3, y - 3, (200, 190, 176))


def plot(L, x, y, damp):
    soil = (128, 84, 56) if damp else (176, 128, 88)
    furrow = (104, 68, 44) if damp else (152, 108, 72)
    L.rect(x - 12, y - 14, x + 12, y, soil)
    for fy in range(y - 12, y - 1, 4):
        L.rect(x - 11, fy, x + 11, fy + 1, furrow)


def plot_damp(L, x, y):
    plot(L, x, y, True)


def plot_dry(L, x, y):
    plot(L, x, y, False)


# Crops: (x, y) the soil they grow from, in the plot's middle.
def crop_seed(L, x, y):
    L.ell(x - 3, y - 3, x + 3, y, (120, 80, 48))
    L.put(x, y - 3, (96, 168, 72))


def crop_sprout(L, x, y):
    L.rect(x, y - 5, x, y, (96, 168, 72))
    L.ell(x - 4, y - 6, x - 1, y - 4, (120, 200, 88)); L.ell(x + 1, y - 7, x + 4, y - 5, (120, 200, 88))


def crop_leaves(L, x, y):
    L.rect(x, y - 10, x, y, (88, 152, 72))
    for k, (dx, dy) in enumerate(((-4, -3), (3, -5), (-3, -8), (3, -10))):
        L.ell(x + dx - 2, y + dy - 1, x + dx + 2, y + dy + 1, (112, 192, 88))


def bud(L, x, y, c):
    crop_leaves(L, x, y)
    L.ell(x - 2, y - 15, x + 2, y - 10, c)


def crop_bud_sun(L, x, y):
    bud(L, x, y, (232, 200, 72))


def crop_bud_tulip(L, x, y):
    bud(L, x, y, (232, 88, 112))


def crop_bud_berry(L, x, y):
    bud(L, x, y, (248, 248, 248))


def crop_cactus_s(L, x, y):
    L.rect(x - 2, y - 5, x + 2, y, (72, 168, 104))
    L.rect(x - 1, y - 5, x - 1, y, (112, 200, 136))


def crop_cactus_m(L, x, y):
    L.rect(x - 2, y - 10, x + 2, y, (72, 168, 104))
    L.rect(x - 1, y - 10, x - 1, y, (112, 200, 136))
    L.rect(x + 3, y - 7, x + 5, y - 5, (72, 168, 104))


def crop_cactus_l(L, x, y):
    crop_cactus_m(L, x, y)
    L.rect(x - 5, y - 9, x - 3, y - 7, (72, 168, 104)); L.rect(x - 5, y - 12, x - 4, y - 9, (72, 168, 104))
    L.ell(x - 2, y - 14, x + 2, y - 10, (248, 120, 180))


def crop_sunflower(L, x, y):
    L.rect(x, y - 18, x, y, (88, 152, 72))
    L.ell(x - 5, y - 9, x - 1, y - 6, (112, 192, 88)); L.ell(x + 1, y - 13, x + 5, y - 10, (112, 192, 88))
    for a in range(0, 360, 45):
        import math as _m
        t = _m.radians(a)
        cx, cy = x + 5 * _m.cos(t), y - 22 + 5 * _m.sin(t)
        L.ell(cx - 2, cy - 2, cx + 2, cy + 2, (255, 208, 64))
    L.ell(x - 3, y - 25, x + 3, y - 19, (136, 84, 40))


def crop_tulip(L, x, y):
    for dx, h, c in ((-4, 12, (232, 72, 104)), (0, 16, (248, 120, 152)), (4, 11, (232, 72, 104))):
        L.rect(x + dx, y - h, x + dx, y, (88, 152, 72))
        L.ell(x + dx - 2, y - h - 5, x + dx + 2, y - h, c)
        L.put(x + dx, y - h - 6, c)
    L.ell(x - 7, y - 4, x - 3, y - 1, (112, 192, 88)); L.ell(x + 3, y - 5, x + 7, y - 2, (112, 192, 88))



def vine(L, x, y):
    for k in range(-9, 10):
        L.put(x + k, y - 2 + int(1.5 * math.sin(k * 0.8)), (88, 152, 72))
    for dx in (-7, 0, 6):
        L.ell(x + dx - 3, y - 7, x + dx + 3, y - 3, (112, 192, 88))


def crop_bud_pumpkin(L, x, y):
    vine(L, x, y)
    L.ell(x - 3, y - 6, x + 3, y, (176, 200, 96))


def crop_pumpkin(L, x, y):
    vine(L, x, y)
    L.ell(x - 9, y - 12, x + 9, y, (240, 140, 48))
    for k in (-4, 0, 4):
        L.rect(x + k, y - 11, x + k, y - 1, (212, 112, 36))
    L.rect(x - 1, y - 15, x + 1, y - 12, (112, 140, 64))


def crop_bud_melon(L, x, y):
    vine(L, x, y)
    L.ell(x - 3, y - 6, x + 3, y, (112, 176, 96))


def crop_melon(L, x, y):
    vine(L, x, y)
    L.ell(x - 10, y - 12, x + 10, y, (64, 152, 72))
    for k in (-6, -2, 2, 6):
        L.rect(x + k, y - 11, x + k + 1, y - 1, (40, 112, 56))
    L.ell(x - 7, y - 10, x - 3, y - 7, (120, 200, 112))


def crop_bud_rose(L, x, y):
    bud(L, x, y, (88, 120, 216))


def crop_rose(L, x, y):
    L.rect(x, y - 16, x, y, (72, 136, 64))
    L.ell(x - 5, y - 9, x - 1, y - 6, (96, 168, 80)); L.ell(x + 1, y - 12, x + 5, y - 9, (96, 168, 80))
    L.ell(x - 5, y - 24, x + 5, y - 15, (72, 104, 216))
    L.ell(x - 3, y - 23, x + 3, y - 17, (112, 148, 240))
    L.ell(x - 1, y - 21, x + 1, y - 19, (60, 84, 192))
    L.put(x + 4, y - 25, (232, 240, 255))


def crop_strawberry(L, x, y):
    L.ell(x - 9, y - 12, x + 9, y, (96, 176, 88))
    L.ell(x - 7, y - 12, x + 2, y - 6, (128, 204, 104))
    for bx, by in ((-5, -5), (1, -3), (5, -7), (-1, -9)):
        L.ell(x + bx - 2, y + by - 2, x + bx + 2, y + by + 2, (232, 56, 72))
        L.put(x + bx - 1, y + by - 1, (255, 200, 120))
    L.ell(x + 3, y - 12, x + 5, y - 10, (255, 255, 255))


def crop_cactus(L, x, y):
    crop_cactus_l(L, x, y)
    L.ell(x - 3, y - 16, x + 3, y - 10, (255, 136, 196))
    L.put(x, y - 13, (255, 232, 120))



# ---------------------------------------------------------------- furniture from the shop

def wall_clock(L, x, y):
    L.ell(x - 6, y - 13, x + 6, y - 1, (176, 120, 80))
    L.ell(x - 5, y - 12, x + 5, y - 2, (252, 248, 236))
    L.rect(x, y - 10, x, y - 7, (60, 60, 72)); L.rect(x, y - 7, x + 3, y - 7, (60, 60, 72))
    for dx, dy in ((0, -11), (4, -7), (0, -3), (-4, -7)):
        L.put(x + dx, y + dy, (176, 120, 80))


def record_player(L, x, y):
    L.rect(x - 8, y - 9, x + 8, y, (168, 112, 72))
    L.rect(x - 8, y - 9, x + 8, y - 8, (196, 140, 92))
    L.ell(x - 7, y - 13, x + 3, y - 8, (48, 48, 60))
    L.ell(x - 4, y - 12, x, y - 9, (232, 96, 96))
    L.rect(x + 5, y - 14, x + 5, y - 9, (200, 200, 216)); L.rect(x + 2, y - 14, x + 5, y - 14, (200, 200, 216))
    L.rect(x - 2, y - 26, x + 2, y - 15, (232, 200, 96))
    L.poly([(x - 2, y - 26), (x - 9, y - 31), (x - 9, y - 20)], (248, 216, 96))


def fairy_lights(L, x, y):
    for k in range(-36, 37):
        sag = int(4 * math.sin((k + 36) / 18 * math.pi) ** 2)
        L.put(x + k, y - 6 + sag, (96, 120, 88))
        if (k + 36) % 9 == 4:
            c = [(255, 216, 96), (248, 140, 180), (140, 200, 248), (160, 232, 140)][((k + 36) // 9) % 4]
            L.rect(x + k - 1, y - 5 + sag, x + k + 1, y - 3 + sag, c)


def teddy(L, x, y):
    c, d = (200, 148, 100), (232, 196, 148)
    L.ell(x - 7, y - 11, x + 7, y, c)
    L.ell(x - 6, y - 20, x + 6, y - 9, c)
    L.ell(x - 7, y - 22, x - 3, y - 17, c); L.ell(x + 3, y - 22, x + 7, y - 17, c)
    L.ell(x - 3, y - 14, x + 3, y - 10, d)
    L.put(x - 2, y - 16, (40, 32, 32)); L.put(x + 2, y - 16, (40, 32, 32)); L.put(x, y - 13, (40, 32, 32))
    L.ell(x - 4, y - 8, x + 4, y - 2, d)
    L.rect(x - 2, y - 9, x + 2, y - 9, (232, 96, 120))


def swing(L, x, y):
    wood = (176, 124, 80)
    L.rect(x - 13, y - 28, x - 11, y, wood); L.rect(x + 11, y - 28, x + 13, y, wood)
    L.rect(x - 14, y - 30, x + 14, y - 28, (148, 100, 64))
    L.rect(x - 6, y - 27, x - 6, y - 8, (200, 200, 200)); L.rect(x + 6, y - 27, x + 6, y - 8, (200, 200, 200))
    L.rect(x - 8, y - 8, x + 8, y - 6, (232, 96, 96))


def windmill(L, x, y, turn):
    L.poly([(x - 9, y), (x - 5, y - 34), (x + 5, y - 34), (x + 9, y)], (240, 232, 216))
    L.rect(x - 3, y - 9, x + 3, y, (168, 112, 72))
    L.rect(x - 2, y - 24, x + 2, y - 19, (120, 176, 232))
    L.poly([(x - 7, y - 33), (x, y - 41), (x + 7, y - 33)], (216, 88, 72))
    cx, cy = x, y - 34
    for k in range(4):
        a = turn + k * math.pi / 2
        for t in range(3, 18):
            px_, py_ = cx + t * math.cos(a), cy + t * math.sin(a)
            L.put(px_, py_, (184, 136, 92))
            if t > 5:
                for w in (1, 2, 3):
                    L.put(px_ + w * math.cos(a + math.pi / 2), py_ + w * math.sin(a + math.pi / 2), (248, 244, 232))
    L.ell(cx - 2, cy - 2, cx + 2, cy + 2, (120, 84, 56))


def windmill_a(L, x, y):
    windmill(L, x, y, 0.3)


def windmill_b(L, x, y):
    windmill(L, x, y, 0.3 + math.pi / 4)



# ---------------------------------------------------------------- the beach

SEA = (96, 168, 224)
SEA_D = (72, 140, 208)
SEA_L = (136, 200, 236)
SAND_B = (240, 222, 172)
SAND_BD = (224, 202, 150)
WET = (214, 192, 146)
BEACH_W = 440   # the beach: west of the yard, nearly three screens


def shore_y(x):
    return 46 + int(3 * math.sin(x * 0.05) + 2 * math.sin(x * 0.13 + 2))


def bg_beach():
    import random
    r = random.Random(11)
    im = Image.new("RGB", (BEACH_W, W), SAND_B)
    px = im.load()
    for x in range(BEACH_W):
        sy = shore_y(x)
        for y in range(sy):
            c = SEA_D if y < 14 else SEA if y < sy - 8 else SEA_L
            if (y < 14 and (x + y * 3) % 17 == 0) or (14 <= y < sy - 8 and (x * 2 + y * 5) % 29 == 0):
                c = SEA_L
            px[x, y] = c
        for y in range(sy, sy + 9):   # wet sand where the waves reach
            px[x, y] = WET if (y < sy + 7 or (x + y) % 2) else SAND_B
    for _ in range(60):   # glints on the water
        x, y = r.randrange(BEACH_W - 3), r.randrange(2, 38)
        if y < shore_y(x) - 4:
            px[x, y] = (232, 244, 252); px[x + 1, y] = (232, 244, 252)
    for _ in range(260):   # the sand's grain
        x, y = r.randrange(BEACH_W), r.randrange(58, W)
        if px[x, y] == SAND_B:
            px[x, y] = SAND_BD
    for _ in range(26):   # pebbles
        x, y = r.randrange(2, BEACH_W - 3), r.randrange(60, W - 3)
        c = r.choice(((200, 190, 176), (176, 168, 160), (232, 200, 200)))
        px[x, y] = c; px[x + 1, y] = c
    return im


def pier(L, x, y):
    """Planks out over the water: a floor mark (no outline) with its posts drawn in."""
    L.rect(x - 7, y - 40, x + 7, y, (176, 128, 84))
    for k in range(y - 40, y + 1, 4):
        L.rect(x - 7, k, x + 7, k, (148, 104, 68))
    for py_ in (y - 38, y - 20):
        L.rect(x - 9, py_, x - 8, py_ + 5, (120, 84, 56)); L.rect(x + 8, py_, x + 9, py_ + 5, (120, 84, 56))


def palm(L, x, y):
    for k in range(26):
        tx = x + int(4 * math.sin(k / 26 * 1.6))
        L.rect(tx - 2, y - k, tx + 1, y - k, (176, 132, 84) if k % 4 else (148, 108, 68))
    tx, ty = x + 4, y - 27
    for a in (-2.6, -2.0, -1.3, -0.4, 0.3, -3.1):
        for t in range(14):
            fx, fy = tx + t * math.cos(a), ty + t * math.sin(a) + t * t * 0.04
            L.rect(fx - 1, fy - 1, fx + 1, fy, (88, 168, 88) if t < 10 else (120, 192, 104))
    L.ell(tx - 3, ty - 1, tx + 1, ty + 3, (140, 96, 60))


def umbrella(L, x, y):
    L.rect(x, y - 24, x, y, (200, 200, 208))
    for k in range(-14, 15):
        h = int(7 * (1 - (k / 14) ** 2))
        c = (232, 88, 88) if (k + 14) // 5 % 2 else (252, 248, 240)
        L.rect(x + k, y - 24 - h, x + k, y - 24, c)
    L.ell(x - 10, y - 4, x + 10, y, (112, 176, 232))
    L.rect(x - 9, y - 3, x + 9, y - 2, (248, 216, 96))


def beach_chair(L, x, y):
    L.rect(x - 8, y - 3, x + 8, y - 2, (176, 128, 84))
    L.poly([(x - 8, y - 4), (x + 4, y - 4), (x + 8, y - 14), (x + 5, y - 14)], (96, 168, 232))
    L.rect(x - 7, y - 1, x - 6, y, (148, 104, 68)); L.rect(x + 6, y - 1, x + 7, y, (148, 104, 68))


def sandcastle(L, x, y):
    c, d = (232, 204, 140), (210, 180, 120)
    L.rect(x - 10, y - 8, x + 10, y, c)
    L.rect(x - 4, y - 16, x + 4, y - 8, c)
    for tx in (x - 10, x + 7):
        L.rect(tx, y - 12, tx + 3, y - 8, d)
    for k in range(-4, 5, 2):
        L.put(x + k, y - 17, c)
    L.rect(x - 1, y - 5, x + 1, y, (176, 140, 92))
    L.rect(x, y - 24, x, y - 16, (120, 84, 56)); L.rect(x + 1, y - 24, x + 4, y - 22, (232, 88, 88))


def lighthouse(L, x, y):
    L.ell(x - 10, y - 4, x + 10, y + 1, (168, 160, 152))
    L.poly([(x - 7, y - 2), (x - 4, y - 44), (x + 4, y - 44), (x + 7, y - 2)], (252, 248, 240))
    for k in (y - 12, y - 28):
        L.poly([(x - 7 + (y - k) * 3 // 44, k), (x + 7 - (y - k) * 3 // 44, k),
                (x + 7 - (y - k + 7) * 3 // 44, k - 7), (x - 7 + (y - k + 7) * 3 // 44, k - 7)], (224, 72, 72))
    L.rect(x - 5, y - 46, x + 5, y - 44, (60, 60, 72))
    L.rect(x - 4, y - 52, x + 4, y - 47, (255, 236, 150))
    L.poly([(x - 6, y - 52), (x, y - 58), (x + 6, y - 52)], (224, 72, 72))
    L.rect(x - 2, y - 8, x + 2, y - 2, (120, 84, 56))


def boat(L, x, y):
    L.poly([(x - 18, y - 9), (x + 18, y - 9), (x + 13, y), (x - 13, y)], (232, 96, 80))
    L.rect(x - 18, y - 10, x + 18, y - 9, (252, 248, 240))
    L.rect(x - 1, y - 30, x, y - 10, (148, 104, 68))
    L.poly([(x + 1, y - 29), (x + 13, y - 12), (x + 1, y - 12)], (252, 248, 240))
    L.rect(x - 10, y - 6, x + 8, y - 5, (196, 72, 64))


def hammock(L, x, y):
    for px_ in (x - 16, x + 16):
        L.rect(px_ - 1, y - 18, px_ + 1, y, (148, 104, 68))
    for k in range(-15, 16):
        sag = int(6 * (1 - (k / 15) ** 2))
        L.rect(x + k, y - 15 + sag, x + k, y - 13 + sag, (248, 168, 88) if (k + 15) // 3 % 2 else (252, 236, 200))


def tide_pool(L, x, y):
    """A pool among the rocks: a floor mark."""
    L.ell(x - 14, y - 9, x + 14, y, (176, 168, 160))
    L.ell(x - 12, y - 8, x + 12, y - 1, (88, 176, 200))
    L.ell(x - 9, y - 7, x - 2, y - 4, (140, 212, 228))
    L.rect(x + 4, y - 5, x + 6, y - 3, (248, 144, 80)); L.put(x + 5, y - 6, (248, 144, 80))


def shell(L, x, y):
    L.ell(x - 4, y - 5, x + 4, y, (248, 196, 200))
    for k in (-2, 0, 2):
        L.rect(x + k, y - 4, x + k, y - 1, (220, 150, 160))
    L.rect(x - 1, y, x + 1, y, (220, 150, 160))


def starfish(L, x, y):
    for a in range(5):
        ang = -math.pi / 2 + a * 2 * math.pi / 5
        for t in range(5):
            L.rect(x + t * math.cos(ang) - 1, y - 4 + t * math.sin(ang), x + t * math.cos(ang), y - 4 + t * math.sin(ang),
                   (248, 152, 96))
    L.put(x, y - 4, (255, 200, 140))


def crab(L, x, y, step):
    c = (232, 88, 72)
    L.ell(x - 5, y - 5, x + 5, y, c)
    L.rect(x - 2, y - 7, x - 2, y - 5, c); L.rect(x + 2, y - 7, x + 2, y - 5, c)
    L.put(x - 2, y - 8, (30, 30, 30)); L.put(x + 2, y - 8, (30, 30, 30))
    for side in (-1, 1):
        L.ell(x + side * 8 - 2, y - 8, x + side * 8 + 2, y - 4, c)
        for k in range(3):
            lx = x + side * (4 + k)
            L.put(lx, y + (1 if (k + step) % 2 else 0), sh(c, 0.8))


def crab_a(L, x, y):
    crab(L, x, y, 0)


def crab_b(L, x, y):
    crab(L, x, y, 1)


def bird_bath(L, x, y):
    L.rect(x - 2, y - 10, x + 2, y, (200, 196, 188))
    L.rect(x - 5, y - 2, x + 5, y, (200, 196, 188))
    L.ell(x - 8, y - 14, x + 8, y - 8, (200, 196, 188))
    L.ell(x - 6, y - 13, x + 6, y - 10, (120, 184, 232))
    L.ell(x + 2, y - 19, x + 7, y - 14, (120, 152, 216)); L.put(x + 8, y - 17, (248, 192, 64))
    L.put(x + 5, y - 18, (30, 30, 30))


# ---------------------------------------------------------------- the woods

MOSS = (132, 184, 108)
MOSS_D = (108, 158, 92)
MOSS_L = (160, 204, 128)
DIRT = (212, 184, 132)
DIRT_D = (188, 158, 110)
CANOPY = (60, 116, 76)
CANOPY_D = (44, 92, 64)
CANOPY_L = (88, 148, 92)
WOODS_W = 520   # the woods: a long walk, three and a third screens


def bg_woods():
    import random
    r = random.Random(7)
    im = Image.new("RGB", (WOODS_W, W), MOSS)
    px = im.load()
    # Behind, the forest itself: trunks under a dark roof of leaves.
    edge = [40 + int(5 * math.sin(x * 0.09) + 3 * math.sin(x * 0.23 + 1)) for x in range(WOODS_W)]
    for x in range(WOODS_W):
        for y in range(edge[x]):
            px[x, y] = CANOPY_D if (y < 10 or (x * 3 + y * 5) % 23 == 0) else CANOPY
    for tx in range(6, WOODS_W, 22):
        tx += r.randrange(-4, 5)
        for y in range(18, edge[min(max(tx, 0), WOODS_W - 1)] + 3):
            for x in range(tx - 3, tx + 4):
                if 0 <= x < WOODS_W:
                    px[x, y] = (112, 84, 64) if x < tx + 2 else (88, 64, 50)
    for _ in range(90):   # leaves in clumps, light from above
        cx, cy, rr = r.randrange(WOODS_W), r.randrange(0, 30), r.randrange(5, 10)
        for y in range(cy - rr, cy + rr):
            for x in range(cx - rr, cx + rr):
                if 0 <= x < WOODS_W and 0 <= y < W and (x - cx) ** 2 + (y - cy) ** 2 < rr * rr:
                    top = (x - cx + 2) ** 2 + (y - cy + 3) ** 2 < (rr - 3) ** 2
                    px[x, y] = CANOPY_L if top else CANOPY
    for x in range(WOODS_W):   # the roof's dark edge, and its shade on the ground
        px[x, edge[x]] = CANOPY_D
        for y in range(edge[x] + 1, edge[x] + 4):
            if (x + y) % 2:
                px[x, y] = MOSS_D

    # The path winds east.
    def path_y(x):
        return 98 + int(7 * math.sin(x * 0.03))
    for x in range(WOODS_W):
        c = path_y(x)
        for y in range(c - 7, c + 8):
            e = y in (c - 7, c + 7)
            if not e or (x + y) % 2:
                px[x, y] = DIRT
        for y in (c - 3, c + 4):
            if (x * 7 + y) % 9 == 0:
                px[x, y] = DIRT_D
    for _ in range(260):   # tufts, light flecks, fallen leaves
        x, y = r.randrange(2, WOODS_W - 3), r.randrange(edge[0] + 6, W - 3)
        if px[x, y] == MOSS:
            px[x, y] = MOSS_D; px[x + 2, y] = MOSS_D; px[x + 1, y + 1] = MOSS_D
    for _ in range(160):
        x, y = r.randrange(WOODS_W), r.randrange(46, W)
        if px[x, y] in (MOSS, DIRT):
            px[x, y] = MOSS_L if px[x, y] == MOSS else (228, 204, 156)
    for _ in range(40):
        x, y = r.randrange(WOODS_W), r.randrange(50, W)
        if px[x, y] == DIRT:
            px[x, y] = r.choice(((216, 140, 72), (200, 104, 64), (232, 176, 88)))
    for _ in range(30):   # small flowers in the moss
        x, y = r.randrange(4, WOODS_W - 4), r.randrange(52, W - 4)
        if px[x, y] == MOSS:
            c = r.choice(((255, 255, 255), (200, 168, 248), (255, 216, 96)))
            for dx, dy in ((0, -1), (-1, 0), (1, 0), (0, 1)):
                if px[x + dx, y + dy] == MOSS:
                    px[x + dx, y + dy] = c
            px[x, y] = (248, 200, 64)
    return im


def oak(L, x, y):
    L.rect(x - 3, y - 12, x + 3, y, (128, 92, 64))
    L.rect(x + 1, y - 12, x + 3, y, (100, 72, 52))
    L.rect(x - 6, y - 2, x + 6, y, (128, 92, 64))
    L.ell(x - 16, y - 40, x + 16, y - 10, (72, 140, 84))
    for k in range(-3, 4):
        L.ell(x + k * 5 - 5, y - 15, x + k * 5 + 5, y - 7, (56, 120, 72))
    L.ell(x - 12, y - 38, x + 2, y - 24, (108, 176, 104))


def bush(L, x, y):
    L.ell(x - 10, y - 10, x + 10, y, (80, 152, 88))
    L.ell(x - 7, y - 10, x + 1, y - 5, (112, 180, 108))


def berry_bush(L, x, y):
    bush(L, x, y)
    for bx, by in ((-6, -6), (-1, -3), (4, -7), (6, -3), (0, -8)):
        L.rect(x + bx, y + by, x + bx + 1, y + by + 1, (88, 96, 216))
        L.put(x + bx, y + by, (168, 176, 248))


def fern(L, x, y):
    for k in range(-2, 3):
        for t in range(7):
            L.put(x + k * t // 2, y - t, (88, 160, 88) if t < 5 else (120, 188, 104))


def mushroom(L, x, y):
    L.rect(x - 1, y - 4, x + 1, y, (240, 228, 200))
    L.ell(x - 4, y - 8, x + 4, y - 3, (224, 72, 72))
    L.put(x - 2, y - 6, (255, 255, 255)); L.put(x + 1, y - 7, (255, 255, 255))


def mushroom_ring(L, x, y):
    for k in range(7):
        a = k * 2 * math.pi / 7
        mx, my = x + int(13 * math.cos(a)), y - 4 + int(5 * math.sin(a))
        L.rect(mx, my - 2, mx, my, (240, 232, 210))
        L.ell(mx - 2, my - 4, mx + 2, my - 2, (176, 132, 232) if k % 2 else (120, 200, 232))
        L.put(mx - 1, my - 3, (232, 240, 255))


def log(L, x, y):
    L.rect(x - 14, y - 7, x + 12, y, (148, 104, 68))
    L.rect(x - 14, y - 7, x + 12, y - 6, (176, 128, 84))
    L.ell(x + 9, y - 7, x + 15, y, (216, 176, 120))
    L.ell(x + 11, y - 5, x + 13, y - 2, (176, 128, 84))
    L.rect(x - 6, y - 9, x - 4, y - 7, (96, 168, 80))


def stump(L, x, y):
    L.rect(x - 6, y - 7, x + 6, y, (140, 100, 66))
    L.ell(x - 6, y - 10, x + 6, y - 5, (216, 180, 124))
    L.ell(x - 3, y - 9, x + 3, y - 6, (188, 148, 100))


def chest(L, x, y, open_):
    L.rect(x - 8, y - 9, x + 8, y, (176, 112, 60))
    L.rect(x - 8, y - 9, x + 8, y - 8, (204, 140, 80))
    for bx in (x - 6, x + 5):
        L.rect(bx, y - 9, bx + 1, y, (232, 192, 80))
    if open_:
        L.rect(x - 8, y - 16, x + 8, y - 10, (148, 92, 52))
        L.rect(x - 7, y - 11, x + 7, y - 9, (60, 40, 32))
        L.put(x - 3, y - 12, (255, 236, 120)); L.put(x + 2, y - 13, (255, 236, 120))
    else:
        L.rect(x - 8, y - 14, x + 8, y - 9, (200, 128, 68))
        L.rect(x - 8, y - 14, x + 8, y - 13, (224, 156, 92))
        for bx in (x - 6, x + 5):
            L.rect(bx, y - 14, bx + 1, y - 9, (232, 192, 80))
        L.rect(x - 1, y - 11, x + 1, y - 7, (248, 216, 96))


def chest_shut(L, x, y):
    chest(L, x, y, False)


def chest_open(L, x, y):
    chest(L, x, y, True)


def pond(L, x, y):
    """A pond: a floor mark (no outline), its rim drawn in."""
    L.ell(x - 22, y - 14, x + 22, y, (176, 160, 128))
    L.ell(x - 20, y - 13, x + 20, y - 1, (96, 160, 216))
    L.ell(x - 16, y - 12, x + 10, y - 7, (136, 192, 232))
    for lx, ly in ((-10, -5), (8, -8), (12, -4)):
        L.ell(x + lx - 3, y + ly - 2, x + lx + 3, y + ly + 1, (96, 172, 88))
    L.put(x + 8, y - 9, (248, 168, 200))


def frog(L, x, y):
    L.ell(x - 3, y - 5, x + 3, y, (112, 192, 88))
    L.put(x - 2, y - 6, (112, 192, 88)); L.put(x + 2, y - 6, (112, 192, 88))
    L.put(x - 2, y - 5, (30, 30, 30)); L.put(x + 2, y - 5, (30, 30, 30))


def tree_house(L, x, y):
    L.rect(x - 4, y - 30, x + 4, y, (128, 92, 64))
    L.rect(x + 1, y - 30, x + 4, y, (100, 72, 52))
    L.rect(x - 8, y - 2, x + 8, y, (128, 92, 64))
    L.ell(x - 22, y - 58, x + 22, y - 22, (72, 140, 84))
    L.ell(x - 16, y - 58, x, y - 44, (108, 176, 104))
    L.rect(x - 12, y - 38, x + 12, y - 36, (176, 124, 80))
    L.rect(x - 9, y - 50, x + 9, y - 38, (224, 172, 112))
    L.poly([(x - 12, y - 49), (x, y - 58), (x + 12, y - 49)], (200, 88, 72))
    L.rect(x - 3, y - 46, x + 3, y - 39, (128, 84, 56))
    L.rect(x + 5, y - 47, x + 8, y - 44, (255, 228, 140))
    for k in range(5):   # the ladder
        L.rect(x - 10, y - 34 + k * 7, x - 6, y - 34 + k * 7, (176, 124, 80))
    L.rect(x - 10, y - 36, x - 10, y, (148, 104, 68)); L.rect(x - 6, y - 36, x - 6, y, (148, 104, 68))


def sign_home(L, x, y):
    L.rect(x - 1, y - 7, x, y, (160, 112, 72))
    L.rect(x - 8, y - 14, x + 8, y - 6, (224, 184, 128))
    for k in range(9):
        L.put(x - 4 + k, y - 10, (120, 80, 48))
    for k in range(3):
        L.put(x - 4 + k, y - 10 - k, (120, 80, 48)); L.put(x - 4 + k, y - 10 + k, (120, 80, 48))


# Slimes, by colour: standing and squashed (hopping, or hit).
SLIMES = {"green": (120, 208, 112), "blue": (104, 168, 240), "pink": (244, 140, 196), "gold": (248, 204, 72)}


def slime(L, x, y, c, squash):
    w, h = (9, 5) if squash else (7, 10)
    L.ell(x - w, y - h, x + w, y, c)
    L.rect(x - w + 1, y - 2, x + w - 1, y, sh(c, 0.85))
    L.ell(x - w + 2, y - h + 1, x - w + 5, y - h + 3, (255, 255, 255))
    ey = y - h + (3 if squash else 4)
    for ex in (x - 2, x + 2):
        L.rect(ex, ey, ex, ey + (0 if squash else 1), (40, 40, 56))
    L.put(x, ey + (2 if not squash else 1), (200, 72, 96))


def slime_fn(c, squash):
    return lambda L, x, y: slime(L, x, y, c, squash)


def puff(L, x, y):
    for px_, py_, r in ((-5, -5, 4), (4, -6, 4), (0, -10, 5), (-1, -3, 3), (5, -2, 3)):
        L.ell(x + px_ - r, y + py_ - r, x + px_ + r, y + py_ + r, (240, 240, 248))
    L.ell(x - 2, y - 12, x + 2, y - 8, (255, 255, 255))


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
    elif kind == "water":
        L.ell(x - 2, y - 1, x + 2, y + 3, (72, 136, 232)); L.poly([(x, y - 4), (x - 2, y), (x + 2, y)], (72, 136, 232))
    elif kind == "plus":
        L.rect(x - 3, y, x + 3, y, (96, 168, 72)); L.rect(x, y - 3, x, y + 3, (96, 168, 72))
    elif kind == "mail":
        L.rect(x - 4, y - 2, x + 4, y + 3, (248, 248, 248)); L.rect(x - 4, y - 2, x + 4, y - 2, c)
        L.put(x - 2, y, c); L.put(x + 2, y, c); L.put(x, y + 1, c)
    elif kind == "house":
        L.poly([(x, y - 4), (x - 4, y), (x + 4, y)], (216, 88, 72)); L.rect(x - 3, y, x + 3, y + 3, (248, 236, 208))
        L.rect(x, y + 1, x, y + 3, (168, 112, 72))
    elif kind == "key":
        L.ell(x - 4, y - 3, x - 1, y, (232, 176, 48)); L.rect(x - 1, y - 2, x + 4, y - 1, (232, 176, 48))
        L.rect(x + 3, y - 1, x + 3, y + 1, (232, 176, 48)); L.rect(x + 1, y - 1, x + 1, y + 1, (232, 176, 48))
    elif kind == "heart":
        L.ell(x - 4, y - 3, x, y + 1, (232, 88, 120)); L.ell(x, y - 3, x + 4, y + 1, (232, 88, 120))
        L.poly([(x - 4, y), (x + 4, y), (x, y + 4)], (232, 88, 120))
    elif kind == "berry":
        for bx, by in ((-2, 0), (2, 0), (0, -2)):
            L.ell(x + bx - 2, y + by - 1, x + bx + 1, y + by + 2, (88, 96, 216))
        L.put(x, y - 4, (96, 168, 72))
    elif kind == "mushroom":
        L.ell(x - 4, y - 4, x + 4, y + 1, (224, 72, 72)); L.rect(x - 1, y + 1, x + 1, y + 3, (240, 228, 200))
        L.put(x - 2, y - 2, (255, 255, 255)); L.put(x + 1, y - 3, (255, 255, 255))
    elif kind == "wave":
        for k in range(-4, 5):
            L.put(x + k, y + int(1.5 * math.sin(k * 1.2)), (72, 140, 208))
            L.put(x + k, y + 3 + int(1.5 * math.sin(k * 1.2 + 1)), (120, 184, 232))
    elif kind == "fish":
        L.ell(x - 4, y - 2, x + 2, y + 2, (96, 152, 216)); L.poly([(x + 2, y), (x + 5, y - 3), (x + 5, y + 3)], (96, 152, 216))
        L.put(x - 2, y - 1, (30, 30, 30))
    elif kind == "shell":
        L.ell(x - 3, y - 3, x + 3, y + 2, (240, 160, 168)); L.rect(x, y - 2, x, y + 1, (200, 120, 132))
    elif kind == "tree":
        L.poly([(x, y - 4), (x - 4, y + 1), (x + 4, y + 1)], (88, 152, 88)); L.rect(x, y + 1, x, y + 3, (140, 96, 60))


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
    ("house_front", house_front, False), ("mailbox", mailbox, False), ("sign", sign, False), ("tree", tree, False),
    ("fruit_tree", fruit_tree, False), ("pine", pine, False), ("fence_h", fence_h, False),
    ("lamp_post", lamp_post, False), ("bench", bench, False), ("flowerbed", flowerbed, False), ("well", well, False),
    ("scarecrow", scarecrow, False), ("coop", coop, False), ("chicken", chicken, False), ("beehive", beehive, False),
    ("barn", barn, False), ("rock", rock, False), ("plot_damp", plot_damp, False), ("plot_dry", plot_dry, False),
    ("crop_seed", crop_seed, False), ("crop_sprout", crop_sprout, False), ("crop_leaves", crop_leaves, False),
    ("crop_bud_sun", crop_bud_sun, False), ("crop_bud_tulip", crop_bud_tulip, False),
    ("crop_bud_berry", crop_bud_berry, False), ("crop_cactus_s", crop_cactus_s, False),
    ("crop_cactus_m", crop_cactus_m, False), ("crop_cactus_l", crop_cactus_l, False),
    ("crop_sunflower", crop_sunflower, False), ("crop_tulip", crop_tulip, False),
    ("crop_strawberry", crop_strawberry, False), ("crop_cactus", crop_cactus, False),
    ("crop_bud_pumpkin", crop_bud_pumpkin, False), ("crop_pumpkin", crop_pumpkin, False),
    ("crop_bud_melon", crop_bud_melon, False), ("crop_melon", crop_melon, False),
    ("crop_bud_rose", crop_bud_rose, False), ("crop_rose", crop_rose, False),
    ("oak", oak, False), ("bush", bush, False), ("berry_bush", berry_bush, False), ("fern", fern, False),
    ("mushroom", mushroom, False), ("mushroom_ring", mushroom_ring, False), ("log", log, False),
    ("stump", stump, False), ("chest_shut", chest_shut, False), ("chest_open", chest_open, False),
    ("pond", pond, True), ("frog", frog, False), ("tree_house", tree_house, False), ("sign_home", sign_home, False),
    *[(f"slime_{k}{'_squash' if q else ''}", slime_fn(c, q), False) for k, c in SLIMES.items() for q in (False, True)],
    ("puff", puff, False),
    ("wall_clock", wall_clock, False), ("record_player", record_player, False),
    ("fairy_lights", fairy_lights, True), ("teddy", teddy, False), ("swing", swing, False),
    ("windmill_a", windmill_a, False), ("windmill_b", windmill_b, False),
    ("pier", pier, True), ("palm", palm, False), ("umbrella", umbrella, False), ("beach_chair", beach_chair, False),
    ("sandcastle", sandcastle, False), ("lighthouse", lighthouse, False), ("boat", boat, False),
    ("hammock", hammock, False), ("tide_pool", tide_pool, True), ("shell", shell, False), ("starfish", starfish, False),
    ("crab_a", crab_a, False), ("crab_b", crab_b, False), ("bird_bath", bird_bath, False),
]
HINTS = ["zzz", "shirt", "game", "pen", "note", "book", "food", "up", "down", "door", "info", "bang", "water", "plus",
         "mail", "tree", "house", "key", "heart", "berry", "mushroom", "wave", "fish", "shell"]
BACKGROUNDS = [("bg_down", bg_down), ("bg_down_fancy", bg_down_fancy), ("bg_up", bg_up), ("bg_up_stars", bg_up_stars),
               ("bg_outside", bg_outside), ("bg_woods", bg_woods), ("bg_beach", bg_beach)]


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

    def pixels(im):
        return im.get_flattened_data() if hasattr(im, "get_flattened_data") else im.getdata()

    seen = {}
    for _, im, *_ in images:
        for r, g, b, a in pixels(im):
            if a:
                seen[(r, g, b)] = seen.get((r, g, b), 0) + 1
    # Up to 255 colours: more, and the nearest are merged (median cut, weighted by use).
    remap = {c: c for c in seen}
    if len(seen) > 255:
        swatch = Image.new("RGB", (len(seen), 1))
        swatch.putdata(list(seen))
        q = swatch.quantize(colors=255, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
        pal = q.getpalette()[:255 * 3]
        idx = list(q.getdata())
        for c, i in zip(seen, idx):
            remap[c] = tuple(pal[i * 3:i * 3 + 3])
    palette = {}
    for c in seen:
        m = remap[c]
        if m not in palette:
            palette[m] = len(palette) + 1

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
        data = bytes(palette[remap[(r, g, b)]] if a else 0 for r, g, b, a in pixels(im))
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
        sheet = Image.new("RGBA", (W * 4 + 50 + OUT_W + WOODS_W + BEACH_W, W + 10 + 70 * 7), (40, 44, 56, 255))
        x = y = 0
        for i, (name, im, *_) in enumerate(images):
            if i < len(BACKGROUNDS):
                sheet.paste(im, (sum(b.width + 10 for _, b, *_ in images[:i]), 0))
                continue
            if x + im.width > sheet.width:
                x, y = 0, y + 52
            sheet.paste(im, (x, W + 10 + y), im)
            x += im.width + 6
        sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST).save(args.png / "world-art.png")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

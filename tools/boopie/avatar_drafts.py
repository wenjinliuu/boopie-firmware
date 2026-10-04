"""Drafts of new looks, for review before they go into avatar_proto.py and the
firmware: GPT redrawn as its logo's rosette, a big head with a little face in
the middle and tiny hands and feet, with skins on that; and themed skins
(海绵宝宝 friends, 蕾姆). Brand characters and these homages are for personal
use only, like the rest of the brand characters here.

  python3 tools/boopie/avatar_drafts.py --out previews/drafts

writes drafts.png (each draft in a few expressions) and drafts.gif.
"""

from __future__ import annotations

import argparse
import math
from pathlib import Path

import numpy as np
from PIL import Image

import avatar_proto as ap
from avatar_proto import (BAYER, N, XS, YS, Boopie, Codex, Doubao, Klaude, Rig, Whale, blush, eye, mix, mouth,
                          ramp)

INK = (18, 18, 24)


# ---------------------------------------------------------------- GPT, the knot as its head
KG = np.array([[1.0 if ch == "#" else 0.0 for ch in row] for row in ap.KNOT])
KIN = np.array([[1.0 if ch != " " else 0.0 for ch in row] for row in ap.KNOT])


def bilinear(grid, gx, gy):
    n = grid.shape[0]
    x = np.clip(gx - 0.5, 0, n - 1.001)
    y = np.clip(gy - 0.5, 0, n - 1.001)
    x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
    fx, fy = x - x0, y - y0
    x1, y1 = np.minimum(x0 + 1, n - 1), np.minimum(y0 + 1, n - 1)
    v = (grid[y0, x0] * (1 - fx) * (1 - fy) + grid[y0, x1] * fx * (1 - fy) + grid[y1, x0] * (1 - fx) * fy
         + grid[y1, x1] * fx * fy)
    return np.where((gx >= 0) & (gx < n) & (gy >= 0) & (gy < n), v, 0)


class Rosette(Rig):
    """GPT: its logo's knot, scaled up smooth, as a big head; a round little
    face in the middle; stubby hands and feet."""
    key, name, colour = "gpt", "GPT", "f2f2f2"
    size, squash_k, jump_k = 1.0, 0.6, 0.6
    line = INK                 # outlines
    holes = (64, 64, 74)       # between the bands
    face_colour = (255, 244, 230)
    scale = 2.45

    def __init__(self, colour=None, skin=None, **kw):
        for k, v in kw.items():
            setattr(self, k, v)
        super().__init__(colour, skin)

    def draw(self, c, p):
        cx, cy = 32, 30
        f = self.frame(p, cx, 54)
        gx, gy = (f.rx - cx) / self.scale + 8, (f.ry - cy) / self.scale + 8
        band = bilinear(KG, gx, gy) > 0.5
        head = bilinear(KIN, gx, gy) > 0.5
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
        c.shaded(band & ~face, self.rp)
        c.outline(band & ~face, self.line)
        c.flat(face, self.face_colour)
        c.outline(face, self.line)
        c.body |= head
        return {"slots": {"hat": f.pt(cx, cy - 18), "eyes": (*f.pt(cx, cy - 1), 4 * f.sx),
                          "neck": (*f.pt(cx, cy + 17), 16 * f.sx)},
                "face": f.pt(cx, cy - 1), "body": f.pt(cx, cy), "light": f.pt(cx + 14, cy - 15),
                "show_face": p.scale > 0.6}

    def face(self, c, fx, fy, p):
        for side in (-1, 1):
            eye(c, fx + side * 3, fy, p, side)
        blush(c, fx, fy - 1, p, 5, self.cheek)
        mouth(c, fx + p.look[0] // 2, fy + 4, p)


# ---------------------------------------------------------------- 海绵宝宝 friends
def hashed(i, k):
    x = math.sin(i * 12.9898 + k * 78.233) * 43758.5453
    return x - math.floor(x)


class Sponge(Klaude):
    """小克 as 海绵宝宝, lightly: yellow, full of holes, little brown shorts;
    everything else 小克's own."""
    def __init__(self):
        super().__init__("f7e14d")
        self.skin = None

    def face(self, c, fx, fy, p):
        c.eye, c.shine = (70, 46, 20), None                  # 小克's square eyes, dark to show on yellow
        for side in (-1, 1):
            eye(c, fx + side * 7, fy, p, side, square=True)
        blush(c, fx, fy, p, 11, (255, 150, 110))
        if p.mouth in ("talk", "o", "chomp", "wavy", "frown"):
            mouth(c, fx, fy + 6, p, colour=(92, 34, 22), inside=(170, 60, 50))

    def draw(self, c, p):
        anchors = super().draw(c, p)
        f = self.frame(p, 32, 51)
        body = f.rect(17, 22, 47, 44)
        for i in range(10):                                  # the holes
            hx, hy = 19 + hashed(i, 1) * 26, 23 + hashed(i, 2) * 13
            if abs(hx - 32) < 11 and 26 < hy < 34:
                continue                                     # not over the face
            hole = f.ellipse(hx, hy, 1.3 + hashed(i, 3) * 0.9, 1.1 + hashed(i, 4) * 0.7)
            c.flat(hole & body, (196, 172, 44))
        c.flat(body & (f.ry >= 40), (150, 96, 44))           # the shorts
        c.flat(body & (f.ry >= 40) & (f.ry < 41), (90, 56, 24))
        return anchors


class Rem(Doubao):
    """豆包 as 蕾姆: a blue bob, a maid's frilly headband, a pink ribbon by the
    ear, blue eyes, a pink hooded jacket over a frilled collar."""
    hair = "8cc0f0"
    top = "f3a6c4"

    def draw(self, c, p):
        anchors = super().draw(c, p)
        f = self.frame(p, 32, 57)
        for i, x in enumerate(range(19, 46, 4)):             # the headband's frills
            yy = 13 - 3.2 * math.cos((x - 32) / 13 * 1.2)
            px, py = f.pt(x, yy)
            for dx, dy in ((0, 0), (1, 0), (0, -1), (1, -1), (-1, 0)):
                c.put(px + dx, py + dy, (252, 252, 252))
            c.put(px, py + 1, (40, 40, 48))
        rx, ry = f.pt(46, 20)                                # the ribbon and its X clip
        for dx, dy in ((-1, -1), (1, 1), (1, -1), (-1, 1), (0, 0)):
            c.put(rx + dx, ry + dy, (230, 70, 150))
        for k in range(2, 8):
            c.put(rx + (k % 2), ry + k, (230, 70, 150))
        cx_, cy_ = f.pt(32, 44)                              # the frilled collar
        for dx in range(-4, 5):
            c.put(cx_ + dx, cy_, (250, 250, 250))
        c.put(cx_, cy_ + 1, (30, 30, 40))
        anchors["light"] = (rx, ry)
        return anchors

    def face(self, c, fx, fy, p):
        super().face(c, fx, fy, p)
        if p.eyes in ("open", "look", "wide", "worried"):
            lx, ly = p.look if p.eyes == "look" else (0, 0)
            c.put(fx + 6 + lx, fy + ly, (70, 120, 220))
            c.put(fx + 6 + lx, fy + 1 + ly, (70, 120, 220))
        # Her fringe falls over one eye, swept down from the parting.
        for y in range(-8, 4):
            edge = -1 - (y + 8) * 0.35
            for x in range(-11, int(round(edge)) + 1):
                strand = (x - y) % 4 == 0
                c.put(fx + x, fy + y, self.hp["dark"] if strand else self.hp["mid"])
            c.put(fx + round(edge) + 1, fy + y, self.hp["out"])
        for x in range(-11, -5):                              # its tip
            c.put(fx + x, fy + 4, self.hp["out"])


class Pearl(Whale):
    """小鲸鱼 as 珍珍: a pale blue-grey whale with a blonde ponytail and a pink bow."""
    belly = ((226, 232, 240), (244, 247, 252))

    def __init__(self):
        super().__init__("aebdd2")

    def draw(self, c, p):
        anchors = super().draw(c, p)
        f = self.frame(p, 31, 56)
        hx, hy = f.pt(29, 25)
        for k in range(7):                                   # the ponytail, up and over
            for w in (-1, 0, 1):
                c.put(hx - k * 0.6 + w, hy - k, (250, 210, 90) if w else (255, 230, 140))
        for dx, dy in ((-2, 0), (-1, 0), (1, 0), (2, 0), (-2, -1), (2, -1), (0, 0)):
            c.put(hx + dx, hy + 1 + dy, (240, 90, 140))
        return anchors


class Karen(Codex):
    """Codex as 凯伦: a grey monitor whose face is a green line on the screen."""
    screen = (8, 14, 10)
    glyph = (90, 240, 120)
    glyph_follows_light = False

    def __init__(self):
        super().__init__("b4b9c4")

    def face(self, c, fx, fy, p):
        g = self.glyph
        amp = 2.5 if p.mouth == "talk" else 1.0 if p.mouth in ("w", "smile") else 0.4
        for x in range(-9, 10):
            y = math.sin((x + p.t * 8) * 0.9) * amp * (1 - abs(x) / 11)
            c.put(fx + x, fy + 1 + y, g)
        if p.eyes not in ("blink", "happy"):
            c.put(fx - 4, fy - 3, g)
            c.put(fx + 4, fy - 3, g)


# ---------------------------------------------------------------- the sheet
DRAFTS = [
    ("GPT A · 白结", lambda: Rosette()),
    ("GPT B · 黑结", lambda: Rosette("26262e", line=(150, 150, 165), holes=(250, 250, 250))),
    ("GPT C · 白结白底", lambda: Rosette(holes=(255, 255, 255))),
    ("GPT D · 经典绿", lambda: Rosette("10a37f", line=(6, 40, 30), holes=(230, 255, 245))),
    ("小克 · 海绵宝宝", Sponge),
    ("豆包 · 蕾姆", Rem),
    ("小鲸鱼 · 珍珍", Pearl),
    ("Codex · 凯伦", Karen),
]
PICKS = [("idle", "待机", 0.4), ("speaking", "说话", 0.4), ("happy", "开心", 0.3), ("sleepy", "犯困", 0.4)]


def main() -> None:
    a = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    a.add_argument("--out", type=Path, required=True)
    args = a.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    lengths = {n: ln for n, ln, _ in ap.EXPRESSIONS}
    cells, labels, at = [], [], []
    for title, make in DRAFTS:
        rig = make()
        for name, zh, still in PICKS:
            cells.append(ap.frames(rig, name, lengths[name], 3))
            labels.append(f"{title} · {zh}")
            at.append(still)
    g = ap.grid(cells, labels, len(PICKS), N * 3, ap.font(15))
    ap.save_gif(g, args.out / "drafts.gif")
    stills = [[fr[int(len(fr) * k)]] for fr, k in zip(cells, at)]
    ap.grid(stills, labels, len(PICKS), N * 3, ap.font(15))[0].save(args.out / "drafts.png")


if __name__ == "__main__":
    main()

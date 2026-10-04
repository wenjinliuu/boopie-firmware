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


# ---------------------------------------------------------------- GPT, the rosette
class Rosette(Rig):
    """GPT: its logo's rosette as a big round head, six bands twisting round a
    middle where its little face is; tiny hands and feet."""
    key, name, colour = "gpt", "GPT", "ececec"
    size, squash_k, jump_k = 1.0, 0.6, 0.6
    line = INK                 # the lines between the bands
    face_colour = "fff4e6"
    hex_face = False           # the face's window: round, or a hexagon like the logo's middle

    def __init__(self, colour=None, skin=None, **kw):
        for k, v in kw.items():
            setattr(self, k, v)
        super().__init__(colour, skin)
        self.fp = ramp(self.face_colour)

    def draw(self, c, p):
        cx, cy, R, r_in = 32, 32, 20.0, 8.5
        f = self.frame(p, cx, 56)
        dx, dy = f.rx - cx, f.ry - cy
        r = np.hypot(dx, dy)
        th = np.arctan2(dy, dx)
        rim = R * (1 - 0.05 * (1 - np.cos(6 * th)) / 2)        # a little hexagonal, as the logo is
        if self.hex_face:
            k = np.cos((np.mod(th + math.pi / 6, math.pi / 3)) - math.pi / 6)
            inner = r * k <= r_in
        else:
            inner = r <= r_in
        head = r <= rim
        bands = head & ~inner
        limbs = np.zeros_like(head)
        if p.feet and p.scale > 0.6:
            limbs |= f.ellipse(cx - 7, 53.5, 3.6, 2.6) | f.ellipse(cx + 7, 53.5, 3.6, 2.6)
        for h in p.hands:
            if h[0] != "front":
                limbs |= f.ellipse(cx + h[0] * (R + 1.5), cy + 7 + h[1], 3, 2.6)
        c.shaded(limbs & ~head, self.rp)
        c.shaded(bands, self.rp)
        # The lines: six from the middle twisting out to the rim, and beside
        # each a shorter one, so the bands look woven over and under.
        t = np.clip((r - r_in) / (R - r_in), 0, 1)
        lines = np.zeros_like(head)
        for k in range(6):
            a0 = math.radians(30 + 60 * k)
            for off, reach in ((0.0, 1.0), (math.radians(26), 0.55)):
                ang = a0 + off + math.radians(58) * t
                d = np.abs(np.angle(np.exp(1j * (th - ang)))) * r
                lines |= bands & (d < 0.65) & (t <= reach)
        c.flat(lines, self.line)
        face = head & inner
        c.flat(face, self.fp["light"])
        c.flat(face & (f.ry > cy + 3) & (BAYER[np.arange(N)[:, None] % 4, np.arange(N)[None, :] % 4] < 0.4),
               self.fp["mid"])
        c.outline(face, self.line)
        c.outline(head | limbs, self.line)
        c.body |= face
        light = f.pt(cx, cy - R - 1)
        return {"slots": {"hat": f.pt(cx, cy - R + 2), "eyes": (*f.pt(cx, cy - 1), 4 * f.sx),
                          "neck": (*f.pt(cx, cy + R - 2), 16 * f.sx)},
                "face": f.pt(cx, cy - 1), "body": f.pt(cx, cy), "light": light, "show_face": p.scale > 0.6}

    def face(self, c, fx, fy, p):
        for side in (-1, 1):
            eye(c, fx + side * 4, fy - 1, p, side)
        blush(c, fx, fy - 1, p, 6, self.cheek)
        mouth(c, fx + p.look[0] // 2, fy + 3, p)


# ---------------------------------------------------------------- 海绵宝宝 friends
def hashed(i, k):
    x = math.sin(i * 12.9898 + k * 78.233) * 43758.5453
    return x - math.floor(x)


class Sponge(Klaude):
    """小克 as 海绵宝宝: a yellow sponge, holes and all, white shirt, red tie,
    brown shorts, thin legs in striped socks, big blue eyes and two buck teeth."""
    def __init__(self):
        super().__init__("f7e14d")

    def draw(self, c, p):
        f = self.frame(p, 32, 53)
        body = f.rect(17, 18, 47, 44)
        for h in p.hands:
            if h[0] != "front":
                up = min(0, h[1]) * 1.6
                x0 = 12 if h[0] < 0 else 47
                body |= f.rect(x0, 33 + up, x0 + 5, 35 + up)
        legs = np.zeros_like(body)
        if p.feet and p.scale > 0.6:
            legs = f.rect(24, 44, 26, 51) | f.rect(38, 44, 40, 51)
        c.shaded(body | legs, self.rp)
        for i in range(9):                                   # the holes
            hx, hy = 19 + hashed(i, 1) * 26, 20 + hashed(i, 2) * 14
            hole = f.ellipse(hx, hy, 1.4 + hashed(i, 3), 1.2 + hashed(i, 4) * 0.8)
            c.flat(hole & body, (190, 168, 40))
        c.flat(body & (f.ry >= 37) & (f.ry < 40) & (f.rx >= 17) & (f.rx < 47), (250, 250, 250))
        shorts = body & (f.ry >= 40) & (f.rx >= 17) & (f.rx < 47)
        c.flat(shorts, (140, 90, 40))
        for x in range(19, 46, 4):                           # the belt
            c.put(*f.pt(x, 41), (40, 26, 14))
        tie = f.rect(31, 37, 33, 42) | f.rect(30, 39, 34, 41)
        c.flat(tie & body, (220, 40, 40))
        if legs.any():
            socks = legs & (f.ry >= 48)
            c.flat(socks, (250, 250, 250))
            c.flat(legs & ((np.round(f.ry) == 48) | (np.round(f.ry) == 49.5)), (60, 110, 220))
            shoes = f.ellipse(25, 52, 3, 1.6) | f.ellipse(39, 52, 3, 1.6)
            c.flat(shoes, (20, 20, 24))
        c.outline(body | legs, (120, 100, 20))
        return {"slots": {"hat": f.pt(32, 19), "eyes": (*f.pt(32, 27), 7 * f.sx), "neck": (*f.pt(32, 37), 30 * f.sx)},
                "face": f.pt(32, 27), "body": f.pt(32, 32), "light": f.pt(44, 14), "show_face": p.scale > 0.6}

    def face(self, c, fx, fy, p):
        big = p.eyes in ("open", "look", "wide", "worried")
        lx, ly = p.look if p.eyes == "look" else (0, 0)
        for side in (-1, 1):
            ex = fx + side * 6
            if big:
                for dy in range(-4, 5):
                    for dx in range(-4, 5):
                        if dx * dx + dy * dy <= 16:
                            c.put(ex + dx, fy + dy, (255, 255, 255))
                for dy in range(-2, 3):
                    for dx in range(-2, 3):
                        if dx * dx + dy * dy <= 4:
                            c.put(ex + dx + lx, fy + dy + ly, (70, 140, 230))
                c.put(ex + lx, fy + ly, (20, 20, 24))
                c.put(ex + lx - 1, fy + ly - 1, (255, 255, 255))
                for k in (-2, 0, 2):                         # lashes
                    c.put(ex + k, fy - 5, (20, 20, 24))
                    c.put(ex + k + (k // 2), fy - 6, (20, 20, 24))
            else:
                c.eye = (20, 20, 24)
                eye(c, ex, fy, p, side)
        for dx in (-9, -8, 8, 9):                            # freckles
            c.put(fx + dx, fy + 5 + (dx % 2), (220, 120, 90))
        my = fy + 7
        if p.mouth in ("talk", "o", "chomp"):
            mouth(c, fx, my - 1, p, colour=(110, 40, 30), inside=(200, 70, 70))
        else:
            for dx in range(-6, 7):
                c.put(fx + dx, my + (1 if abs(dx) < 5 else 0), (110, 40, 30))
        for dx in (-1, 1):                                   # buck teeth
            c.put(fx + dx, my + 2, (255, 255, 255))
            c.put(fx + dx, my + 3, (255, 255, 255))


class Star(Boopie):
    """布比 as 派大星: a pink star of a fellow, pointy head, green shorts with
    purple flowers, thick brows."""
    def __init__(self):
        super().__init__("f6a3b8")

    def draw(self, c, p):
        cx, cy, rx, ry = 32, 40, 15, 14
        f = self.frame(p, cx, cy + ry)
        body = f.ellipse(cx, cy, rx, ry)
        tip = (f.ry > 14) & (f.ry < cy) & (np.abs(f.rx - cx) < (f.ry - 14) * 0.62)
        body |= tip
        if p.feet and p.scale > 0.6:
            body |= f.ellipse(cx - 7, cy + ry - 1, 4, 2.5) | f.ellipse(cx + 7, cy + ry - 1, 4, 2.5)
        for h in p.hands:
            if h[0] != "front":
                body |= f.rot_ellipse(cx + h[0] * (rx + 1), cy + 1 + h[1], 5, 2.4, h[0] * 0.5)
        c.shaded(body, self.rp)
        shorts = body & (f.ry >= cy + 5) & (f.ry < cy + ry - 1)
        c.flat(shorts, (130, 200, 80))
        for i in range(5):
            x, y = f.pt(cx - 10 + i * 5, cy + 8 + (i % 2) * 2)
            c.put(x, y, (170, 90, 210))
            c.put(x + 1, y, (170, 90, 210))
        c.outline(body, (170, 70, 100))
        light = f.pt(cx, 13)
        return {"slots": {"hat": f.pt(cx, 18), "eyes": (*f.pt(cx, cy - 4), 6 * f.sx), "neck": (*f.pt(cx, cy + 5), 26 * f.sx)},
                "face": f.pt(cx, cy - 4), "body": f.pt(cx, cy), "light": light, "show_face": p.scale > 0.6}

    def face(self, c, fx, fy, p):
        c.eye = (20, 20, 24)
        for side in (-1, 1):
            eye(c, fx + side * 4, fy, p, side)
            for dx in range(-1, 3):                          # brows
                c.put(fx + side * (3 + dx), fy - 4 - (dx == 2), (20, 20, 24))
        mouth(c, fx, fy + 5, p, colour=(120, 30, 50), inside=(200, 70, 90))


class Squirrel(Doubao):
    """豆包 as 珊迪: a squirrel in a white suit inside a glass helmet with a flower on top."""
    hair = "b8743e"
    top = "f2f2f2"

    def __init__(self):
        super().__init__("e6b07c")

    def draw(self, c, p):
        anchors = super().draw(c, p)
        f = self.frame(p, 32, 57)
        for sx in (-1, 1):                                   # ears
            ear = (f.ry > 8) & (f.ry < 14) & (np.abs(f.rx - (32 + sx * 11)) < (f.ry - 8) * 0.55)
            c.shaded(ear, self.hp)
        mz = f.ellipse(32, 35, 6, 3.5)                       # the white muzzle
        c.flat(mz, (250, 244, 236))
        helmet = f.ellipse(32, 28, 21, 21)
        ring = helmet & ~f.ellipse(32, 28, 20, 20)
        c.flat(ring & (f.ry < 45), (190, 230, 255))
        for a in range(200, 250, 6):                         # a glint
            x, y = f.pt(32 + 17 * math.cos(math.radians(a)), 28 + 17 * math.sin(math.radians(a)))
            c.put(x, y, (255, 255, 255))
        fx, fy = f.pt(32, 6)                                 # the flower
        for dx, dy in ((-2, 0), (2, 0), (0, -2), (0, 2), (-1, -1), (1, 1), (-1, 1), (1, -1)):
            c.put(fx + dx, fy + dy, (190, 110, 220))
        c.put(fx, fy, (255, 220, 80))
        anchors["light"] = (fx, fy)
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
            for side in (-1, 1):
                c.put(fx + side * 6 + lx, fy + ly, (70, 120, 220))
                c.put(fx + side * 6 + lx, fy + 1 + ly, (70, 120, 220))


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
    ("GPT 新造型 · 圆脸", lambda: Rosette()),
    ("GPT 新造型 · 六边形脸", lambda: Rosette(hex_face=True)),
    ("GPT · 经典绿", lambda: Rosette("10a37f", line=(8, 50, 40), face_colour="fff4e6")),
    ("GPT · 夜黑", lambda: Rosette("2c2c34", line=(200, 200, 214), face_colour="fff4e6")),
    ("GPT · 流金（典藏）", lambda: Rosette("f2c14e", line=(110, 70, 10))),
    ("GPT · 青花", lambda: Rosette("f4f6fa", line=(50, 80, 180), face_colour="ffffff")),
    ("小克 · 海绵宝宝", Sponge),
    ("布比 · 派大星", Star),
    ("豆包 · 珊迪", Squirrel),
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

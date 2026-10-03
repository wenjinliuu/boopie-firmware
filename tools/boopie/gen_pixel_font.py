#!/usr/bin/env python3
# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0

"""Builds Boopie's bilingual pixel font from Ark Pixel Font's 12 px glyphs.

  git clone --filter=blob:none https://github.com/TakWolf/ark-pixel-font.git
  git clone --filter=blob:none https://github.com/TakWolf/fusion-pixel-font.git
  python3 tools/boopie/gen_pixel_font.py --ark ark-pixel-font --fusion fusion-pixel-font

writes esp32/components/boopie/font/boopie_pixel_glyphs.c: every GBK hanzi,
every GB2312 character, ASCII, Latin-1 and full-width forms, as 12 x 12 one-bit
cells (Latin glyphs fill the left 6 columns). Ark is still missing characters,
so this fills them the way Fusion Pixel Font does: Fusion's patches to Ark
first, then Ark, then Cubic 11 rasterized at 12 px. Simplified Chinese shapes
win where there are regional variants. A glyph whose width disagrees with
boopie_text.c's column rule is left out, so layout and drawing always agree.
Needs Pillow and fontTools (with brotli, for Cubic 11's WOFF2).
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
TEXT_C = ROOT / "esp32" / "components" / "boopie" / "boopie_text.c"
OUT = ROOT / "esp32" / "components" / "boopie" / "font" / "boopie_pixel_glyphs.c"

CELL = 12
LANGS = ["zh_cn", "zh_hans"]   # preferred regional variants, best first


def wide_ranges() -> list[tuple[int, int]]:
    src = TEXT_C.read_text()
    table = src[src.index("BEGIN WIDE"):src.index("END WIDE")]
    return [(int(lo, 16), int(hi, 16))
            for lo, hi in re.findall(r"\{\s*0x([0-9A-Fa-f]+),\s*0x([0-9A-Fa-f]+)\s*\}", table)]


def wanted() -> set[int]:
    cps = set(range(0x20, 0x7F)) | set(range(0xA0, 0x250))   # Latin, with pinyin's tone marks
    cps |= set(range(0x2014, 0x2017)) | set(range(0x3000, 0x3040))
    cps |= set(range(0xFF01, 0xFF5F)) | set(range(0xFFE0, 0xFFE6))
    for b1 in range(0xA1, 0xF8):            # all of GB2312
        for b2 in range(0xA1, 0xFF):
            try:
                cps.add(ord(bytes([b1, b2]).decode("gb2312")))
            except UnicodeDecodeError:
                pass
    for b1 in range(0x81, 0xFF):            # GBK's hanzi
        for b2 in range(0x40, 0xFF):
            if b2 == 0x7F:
                continue
            try:
                ch = bytes([b1, b2]).decode("gbk")
            except UnicodeDecodeError:
                continue
            if 0x4E00 <= ord(ch) <= 0x9FFF:
                cps.add(ord(ch))
    return {cp for cp in cps if cp <= 0xFFFF}


def index_glyphs(glyph_dir: Path) -> dict[int, list[tuple[Path, list[str]]]]:
    """Every 12 px glyph file under glyph_dir by code point, with the languages it's for."""
    found: dict[int, list[tuple[Path, list[str]]]] = {}
    for png in (glyph_dir / "cmap").rglob("*.png"):
        m = re.fullmatch(r"([0-9A-F]{4,5})(?: (.+))?", png.stem)
        if not m:
            continue
        langs = m.group(2).split(",") if m.group(2) else []
        found.setdefault(int(m.group(1), 16), []).append((png, langs))
    return found


def pick(files: list[tuple[Path, list[str]]], width: int) -> Image.Image | None:
    def rank(item: tuple[Path, list[str]]) -> int:
        _, langs = item
        for i, lang in enumerate(LANGS):
            if lang in langs:
                return i
        return len(LANGS) if not langs else len(LANGS) + 1
    for png, _ in sorted(files, key=rank):
        im = Image.open(png).convert("RGBA")
        if im.size == (width, CELL):
            return im
    return None


class Cubic11:
    """Cubic 11 drawn at 12 px without smoothing, placed as Fusion places it."""

    def __init__(self, path: Path) -> None:
        self.cmap = TTFont(path).getBestCmap()
        self.font = ImageFont.truetype(str(path), CELL)

    def glyph(self, cp: int) -> Image.Image | None:
        if cp not in self.cmap:
            return None
        im = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
        draw = ImageDraw.Draw(im)
        draw.fontmode = "1"
        draw.text((-1, 10), chr(cp), font=self.font, fill=(0, 0, 0, 255), anchor="ls")
        return im if im.getchannel("A").getbbox() else None


def synthesized(cp: int) -> Image.Image | None:
    """Full-width dashes, which no source has: a rule across the cell, so that
    "——" joins up, on row 6 like Ark's hyphen."""
    if cp not in (0x2014, 0x2015):
        return None
    im = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
    for x in range(CELL):
        im.putpixel((x, 6), (0, 0, 0, 255))
    return im


def cell_bits(im: Image.Image) -> bytes:
    bits = []
    for y in range(CELL):
        for x in range(CELL):
            if x < im.width:
                r, g, b, a = im.getpixel((x, y))
                bits.append(1 if a > 127 and r + g + b < 384 else 0)
            else:
                bits.append(0)
    return bytes(int("".join(map(str, bits[i:i + 8])), 2) for i in range(0, len(bits), 8))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--ark", type=Path, required=True, help="ark-pixel-font checkout")
    ap.add_argument("--fusion", type=Path, required=True, help="fusion-pixel-font checkout")
    ap.add_argument("--out", type=Path, default=OUT)
    args = ap.parse_args()

    wide = wide_ranges()
    is_wide = lambda cp: any(lo <= cp <= hi for lo, hi in wide)
    sources = [index_glyphs(args.fusion / "assets" / "patch-glyphs" / "12"),
               index_glyphs(args.ark / "assets" / "glyphs" / "12")]
    cubic = Cubic11(args.fusion / "assets" / "fonts" / "cubic-11" / "Cubic_11.woff2")
    commit = lambda repo: subprocess.run(["git", "-C", str(repo), "rev-parse", "HEAD"],
                                         capture_output=True, text=True).stdout.strip() or "unknown"

    cps, cells, wides, missing, mismatched = [], [], [], [], []
    from_cubic = 0
    for cp in sorted(wanted()):
        w = 2 if is_wide(cp) else 1
        found = [files for files in (src.get(cp) for src in sources) if files]
        im = next((g for g in (pick(files, CELL // 2 * w) for files in found) if g), None)
        if im is None and w == 2:
            im = cubic.glyph(cp)
            from_cubic += im is not None
        if im is None and w == 2:
            im = synthesized(cp)
        if im is None:
            (mismatched if found else missing).append(cp)
            continue
        cps.append(cp)
        cells.append(cell_bits(im))
        wides.append(w == 2)

    n = len(cps)
    wide_bits = bytearray((n + 7) // 8)
    for i, w in enumerate(wides):
        if w:
            wide_bits[i // 8] |= 0x80 >> (i % 8)

    def rows(data: bytes, per: int) -> str:
        return "\n".join("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + per]) + ","
                         for i in range(0, len(data), per))

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w") as f:
        f.write(f"""/*
 * Generated by tools/boopie/gen_pixel_font.py. Do not edit.
 *
 * Glyphs from Ark Pixel Font 12 px (https://github.com/TakWolf/ark-pixel-font,
 * commit {commit(args.ark)}) with Fusion Pixel Font's patches
 * (https://github.com/TakWolf/fusion-pixel-font, commit {commit(args.fusion)}),
 * Copyright (c) 2021, TakWolf, and {from_cubic} hanzi from Cubic 11
 * (https://github.com/ACh-K/Cubic-11), Copyright (c) 2021-2025, ACh-K. All under
 * the SIL Open Font License 1.1: see the OFL-*.txt files next to this file.
 *
 * {n} glyphs; {len(missing)} wanted characters have no glyph anywhere and
 * {len(mismatched)} have none of the width boopie_text.c expects.
 */

#include "boopie_pixel_font.h"

const uint32_t boopie_pixel_glyph_count = {n};

/* Code points, ascending. */
const uint16_t boopie_pixel_cps[{n}] = {{
""")
        for i in range(0, n, 12):
            f.write("    " + ", ".join(f"0x{cp:04x}" for cp in cps[i:i + 12]) + ",\n")
        f.write(f"""}};

/* Bit i (MSB first): glyph i is two columns wide. */
const uint8_t boopie_pixel_wide[{len(wide_bits)}] = {{
{rows(bytes(wide_bits), 16)}
}};

/* 12 x 12 cells, one bit a pixel, rows top down, MSB first: 18 bytes each. */
const uint8_t boopie_pixel_bits[{n * 18}] = {{
{rows(b"".join(cells), 18)}
}};
""")
    print(f"{n} glyphs ({sum(wides)} wide, {from_cubic} from Cubic 11), {len(missing)} missing, "
          f"{len(mismatched)} wrong width -> {args.out.relative_to(ROOT)}", file=sys.stderr)
    if mismatched:
        print("wrong width: " + " ".join(chr(cp) for cp in mismatched[:80]), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())

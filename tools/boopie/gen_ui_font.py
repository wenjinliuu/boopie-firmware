#!/usr/bin/env python3
# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0

"""Builds the UI's smooth Chinese font for the assets partition.

  curl -LO https://raw.githubusercontent.com/notofonts/noto-cjk/main/Sans/SubsetOTF/SC/NotoSansSC-Regular.otf
  pip install fonttools
  python3 tools/boopie/gen_ui_font.py --source NotoSansSC-Regular.otf

writes esp32/assets/fonts/ui.otf: Noto Sans SC (思源黑体, SIL OFL 1.1) cut to
what Boopie shows: every GB2312 character, ASCII and Latin-1, general and
CJK punctuation, full-width forms, and the symbols the UI uses (★ ☆ …),
with hinting and the layout tables a single-line label doesn't use dropped.
LVGL's TinyTTF draws it at any size, anti-aliased.
"""

from __future__ import annotations

import argparse
from pathlib import Path

from fontTools import subset
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "esp32" / "assets" / "fonts" / "ui.otf"


def wanted() -> set[int]:
    cps = set(range(0x20, 0x7F)) | set(range(0xA0, 0x100))
    cps |= set(range(0x2000, 0x2070)) | set(range(0x3000, 0x3040)) | set(range(0xFF00, 0xFFF0))
    cps |= {0x2605, 0x2606, 0x25CB, 0x25CF, 0x2190, 0x2191, 0x2192, 0x2193, 0x00B7, 0x2103}
    for b1 in range(0xA1, 0xF8):
        for b2 in range(0xA1, 0xFF):
            try:
                cps.add(ord(bytes([b1, b2]).decode("gb2312")))
            except UnicodeDecodeError:
                pass
    return cps


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", type=Path, required=True, help="NotoSansSC-Regular.otf")
    args = ap.parse_args()
    font = TTFont(args.source)
    opts = subset.Options()
    opts.hinting = False
    opts.layout_features = ["kern"]
    opts.name_IDs = [0, 1, 2, 3, 4, 5, 6, 13, 14]   # keep the copyright and licence names
    opts.notdef_outline = True
    sub = subset.Subsetter(opts)
    sub.populate(unicodes=wanted())
    sub.subset(font)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    font.save(OUT)
    print(f"{len(font.getBestCmap())} characters, {OUT.stat().st_size // 1024} KB -> {OUT}")


if __name__ == "__main__":
    main()

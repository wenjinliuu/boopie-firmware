#!/usr/bin/env python3
# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0

"""Packs esp32/assets/ into the assets partition's image (store/boopie_assets.h).

  python3 tools/boopie/pack_assets.py --out build/assets.bin [--dir esp32/assets] [--max 10M] [--firmware-data]

A 32-byte header (magic BPK1, version from VERSION, entry count, pack size,
CRC-32 of the entries), the entries (name, offset, size, CRC-32 of the
data), then the files, each 16-byte aligned. README.md and VERSION stay out.

With --firmware-data the pack also carries what the device reads from flash
instead of keeping in PSRAM (docs/boopie-storage.md), taken from the sources
the simulator builds in, so the two can't drift apart:
  fonts/ui.otf         components/boopie/font/ui.otf
  fonts/pixel12.bits   boopie_pixel_bits[] of font/boopie_pixel_glyphs.c
  world/bg_<ID>.px     the PX_BG_<ID>[] backgrounds of world/boopie_world_art.c
"""

from __future__ import annotations

import argparse
import re
import struct
import zlib
from pathlib import Path

MAGIC = 0x314B5042   # "BPK1"
NAME = 48
HEADER = struct.Struct("<8I")
ENTRY = struct.Struct(f"<{NAME}s4I")
SKIP = {"README.md", "VERSION"}


BOOPIE = Path(__file__).resolve().parents[2] / "esp32" / "components" / "boopie"


def c_array(src: str, name: str) -> bytes:
    """The bytes of `const uint8_t name[N] = { ... };` in a generated C file."""
    m = re.search(r"\b" + re.escape(name) + r"\[(\d+)\]\s*=\s*\{(.*?)\};", src, re.S)
    if not m:
        raise SystemExit(f"{name} not found")
    data = bytes(int(v, 0) for v in m.group(2).replace("\n", " ").split(",") if v.strip())
    if len(data) != int(m.group(1)):
        raise SystemExit(f"{name}: {len(data)} bytes, not {m.group(1)}")
    return data


def firmware_data() -> dict[str, bytes]:
    out = {"fonts/ui.otf": (BOOPIE / "font" / "ui.otf").read_bytes()}
    out["fonts/pixel12.bits"] = c_array((BOOPIE / "font" / "boopie_pixel_glyphs.c").read_text(), "boopie_pixel_bits")
    art = (BOOPIE / "world" / "boopie_world_art.c").read_text()
    for bg in re.findall(r"static const uint8_t PX_BG_(\w+)\[", art):
        out[f"world/bg_{bg.lower()}.px"] = c_array(art, "PX_BG_" + bg)
    return out


def pack(src: Path, extra: dict[str, bytes] | None = None) -> bytes:
    version = int((src / "VERSION").read_text().strip())
    files = {p.relative_to(src).as_posix(): p for p in src.rglob("*")
             if p.is_file() and p.relative_to(src).as_posix() not in SKIP}
    files.update(extra or {})
    offset = HEADER.size + ENTRY.size * len(files)
    entries, blobs = [], []
    for key in sorted(files):
        p = files[key]
        name = key.encode()
        if len(name) >= NAME:
            raise SystemExit(f"{name.decode()}: names are under {NAME} bytes")
        offset = (offset + 15) & ~15
        data = p if isinstance(p, bytes) else p.read_bytes()
        entries.append(ENTRY.pack(name, offset, len(data), zlib.crc32(data) & 0xFFFFFFFF, 0))
        blobs.append((offset, data))
        offset += len(data)
    table = b"".join(entries)
    out = bytearray(offset)
    out[HEADER.size:HEADER.size + len(table)] = table
    for off, data in blobs:
        out[off:off + len(data)] = data
    out[:HEADER.size] = HEADER.pack(MAGIC, version, len(files), len(out), zlib.crc32(table) & 0xFFFFFFFF, 0, 0, 0)
    return bytes(out)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", type=Path, default=Path(__file__).resolve().parents[2] / "esp32" / "assets")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--max", default="10M", help="the partition's size")
    ap.add_argument("--firmware-data", action="store_true", help="add the font and art the device reads from flash")
    args = ap.parse_args()
    data = pack(args.dir, firmware_data() if args.firmware_data else None)
    limit = int(args.max[:-1]) * (1 << 20) if args.max.endswith("M") else int(args.max, 0)
    if len(data) > limit:
        raise SystemExit(f"assets are {len(data)} bytes, over the partition's {limit}")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(data)
    print(f"assets v{(args.dir / 'VERSION').read_text().strip()}: {len(data)} bytes -> {args.out}")


if __name__ == "__main__":
    main()

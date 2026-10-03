#!/usr/bin/env python3
# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0

"""Packs esp32/assets/ into the assets partition's image (store/boopie_assets.h).

  python3 tools/boopie/pack_assets.py --out build/assets.bin [--dir esp32/assets] [--max 10M]

A 32-byte header (magic BPK1, version from VERSION, entry count, pack size,
CRC-32 of the entries), the entries (name, offset, size, CRC-32 of the
data), then the files, each 16-byte aligned. README.md and VERSION stay out.
"""

from __future__ import annotations

import argparse
import struct
import zlib
from pathlib import Path

MAGIC = 0x314B5042   # "BPK1"
NAME = 48
HEADER = struct.Struct("<8I")
ENTRY = struct.Struct(f"<{NAME}s4I")
SKIP = {"README.md", "VERSION"}


def pack(src: Path) -> bytes:
    version = int((src / "VERSION").read_text().strip())
    files = sorted(p for p in src.rglob("*") if p.is_file() and p.relative_to(src).as_posix() not in SKIP)
    offset = HEADER.size + ENTRY.size * len(files)
    entries, blobs = [], []
    for p in files:
        name = p.relative_to(src).as_posix().encode()
        if len(name) >= NAME:
            raise SystemExit(f"{name.decode()}: names are under {NAME} bytes")
        offset = (offset + 15) & ~15
        data = p.read_bytes()
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
    args = ap.parse_args()
    data = pack(args.dir)
    limit = int(args.max[:-1]) * (1 << 20) if args.max.endswith("M") else int(args.max, 0)
    if len(data) > limit:
        raise SystemExit(f"assets are {len(data)} bytes, over the partition's {limit}")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(data)
    print(f"assets v{(args.dir / 'VERSION').read_text().strip()}: {len(data)} bytes -> {args.out}")


if __name__ == "__main__":
    main()

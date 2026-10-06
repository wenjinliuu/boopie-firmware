#!/usr/bin/env python3
# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0

"""Lays out one OTA release for the R2 bucket the OTA worker serves.

  python3 tools/boopie/ota_release.py --version 1.2.0 --app build/muse-gadget.bin \\
      --assets build/boopie_assets.bin --assets-version 3 --notes notes.txt --out ota/

writes ota/files/boopie-<version>.bin, ota/files/assets-<n>-<sha8>.bin and
ota/manifest.json. Signing is done after, with openssl and the release key,
which this script never sees, then the signature is made raw (r || s, 64 bytes),
which is what the board checks:

  openssl dgst -sha256 -sign key.pem -out sig.der ota/manifest.json
  python3 tools/boopie/ota_release.py rawsig sig.der ota/manifest.sig

The board checks that signature against the public key built into it, then
each file's SHA-256 against the manifest. It installs an app whose version is
newer than its own, and assets whose version is newer than the pack it has.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import sys
import time
from pathlib import Path

VERSION = re.compile(r"^\d+\.\d+\.\d+$")


def entry(src: Path, dst: Path) -> dict:
    data = src.read_bytes()
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(src, dst)
    return {"file": dst.name, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def rawsig(src: Path, dst: Path) -> None:
    """An ECDSA signature from DER (SEQUENCE of two INTEGERs) to r || s."""
    d = src.read_bytes()

    def length(i: int) -> tuple[int, int]:
        n = d[i]
        if n < 0x80:
            return n, i + 1
        k = n & 0x7F
        return int.from_bytes(d[i + 1:i + 1 + k], "big"), i + 1 + k

    if d[0] != 0x30:
        raise SystemExit("not a DER signature")
    _, i = length(1)
    out = b""
    for _ in range(2):
        if d[i] != 0x02:
            raise SystemExit("not a DER signature")
        n, i = length(i + 1)
        v = int.from_bytes(d[i:i + n], "big")
        i += n
        out += v.to_bytes(32, "big")
    dst.write_bytes(out)


def main() -> None:
    if len(sys.argv) == 4 and sys.argv[1] == "rawsig":
        rawsig(Path(sys.argv[2]), Path(sys.argv[3]))
        return
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", required=True, help="x.y.z, the app's PROJECT_VER")
    ap.add_argument("--app", type=Path, required=True)
    ap.add_argument("--assets", type=Path, required=True)
    ap.add_argument("--assets-version", type=int, required=True)
    ap.add_argument("--notes", type=Path, help="what's new, a few lines (UTF-8)")
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()
    if not VERSION.match(args.version):
        raise SystemExit(f"version {args.version!r} isn't x.y.z")
    notes = args.notes.read_text(encoding="utf-8").strip() if args.notes and args.notes.exists() else ""
    files = args.out / "files"
    app = entry(args.app, files / f"boopie-{args.version}.bin")
    sha = hashlib.sha256(args.assets.read_bytes()).hexdigest()
    assets = entry(args.assets, files / f"assets-{args.assets_version}-{sha[:8]}.bin")
    assets["version"] = args.assets_version
    manifest = {
        "schema": 1,
        "board": "waveshare-s3-175c",
        "version": args.version,
        "date": time.strftime("%Y-%m-%d", time.gmtime()),
        "notes": notes[:600],
        "app": app,
        "assets": assets,
    }
    (args.out / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    print(json.dumps(manifest, ensure_ascii=False, indent=1))


if __name__ == "__main__":
    main()

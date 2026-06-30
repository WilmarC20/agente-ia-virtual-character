#!/usr/bin/env python3
"""Build RGB565 atlas binaries and coordinate JSON from PNG sources.

Usage:
  python tools/atlas_builder.py --theme kitt --input themes/kitt/source/labels.png

Outputs (by default):
  themes/<theme>/labels_atlas.bin  — raw RGB565 LE
  themes/<theme>/labels_atlas.json — sprite rects { "POWER": {x,y,w,h}, ... }
"""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

try:
    from PIL import Image
except ImportError as exc:
    raise SystemExit("Pillow required: pip install pillow") from exc


def rgb888_to_rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def image_to_rgb565_bin(img: Image.Image) -> bytes:
    img = img.convert("RGB")
    out = bytearray()
    for y in range(img.height):
        for x in range(img.width):
            r, g, b = img.getpixel((x, y))
            out += struct.pack("<H", rgb888_to_rgb565(r, g, b))
    return bytes(out)


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description="PNG → RGB565 atlas builder")
    parser.add_argument("--theme", default="kitt")
    parser.add_argument("--input", type=Path, help="Source PNG (optional for scaffold)")
    parser.add_argument("--out-dir", type=Path, help="Override output directory")
    args = parser.parse_args()

    theme_dir = args.out_dir or (root / "themes" / args.theme)
    theme_dir.mkdir(parents=True, exist_ok=True)

    manifest = {
        "theme": args.theme,
        "format": "rgb565_le",
        "generated": True,
        "sprites": {},
    }

    if args.input and args.input.exists():
        img = Image.open(args.input)
        bin_path = theme_dir / "labels_atlas.bin"
        bin_path.write_bytes(image_to_rgb565_bin(img))
        manifest["width"] = img.width
        manifest["height"] = img.height
        manifest["bin"] = bin_path.name
        print(f"Wrote {bin_path} ({len(bin_path.read_bytes())} bytes)")
    else:
        print(f"No input PNG — wrote scaffold manifest only in {theme_dir}")

    json_path = theme_dir / "labels_atlas.json"
    json_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"Wrote {json_path}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Build RGB565 KITT dashboard atlas from reference PNG.

Usage:
  python tools/atlas_builder.py --theme kitt

Reads:
  themes/kitt/source/dashboard_ref.png
  themes/kitt/layout.json

Writes:
  themes/kitt/labels_atlas.bin
  themes/kitt/labels_atlas.json
  firmware/agente-ia/aura/kitt_atlas_embed.h
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import struct
from pathlib import Path

try:
    from PIL import Image, ImageDraw
except ImportError as exc:
    raise SystemExit("Pillow required: pip install pillow") from exc

CANVAS_W = 240
CANVAS_H = 320


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


def modulator_bbox(layout: dict) -> tuple[int, int, int, int]:
    m = layout["modulator"]
    seg_w, seg_h, seg_gap = m["segW"], m["segH"], m["segGap"]
    center_y = m["centerY"]
    cols = [
        (m["cxLeft"], m["segSide"]),
        (m["cxMid"], m["segMid"]),
        (m["cxRight"], m["segSide"]),
    ]
    min_x, min_y = 9999, 9999
    max_x, max_y = 0, 0
    for cx, max_seg in cols:
        total_h = max_seg * seg_h + (max_seg - 1) * seg_gap
        top_y = center_y - total_h // 2
        x0 = cx - seg_w // 2
        min_x = min(min_x, x0)
        max_x = max(max_x, x0 + seg_w)
        min_y = min(min_y, top_y)
        max_y = max(max_y, top_y + total_h)
    pad = 3
    return min_x - pad, min_y - pad, (max_x - min_x) + 2 * pad, (max_y - min_y) + 2 * pad


def sprite_rects(layout: dict) -> dict[str, dict[str, int]]:
    t = layout["topBars"]
    o = layout["ovals"]
    b = layout["bottomBars"]
    mx, my, mw, mh = modulator_bbox(layout)
    sprites: dict[str, dict[str, int]] = {
        "dashboard_bg": {"x": 0, "y": 0, "w": CANVAS_W, "h": CANVAS_H},
        "modulator_zone": {"x": mx, "y": my, "w": mw, "h": mh},
    }
    for i in range(4):
        sprites[f"top_{i}"] = {
            "x": t["x"],
            "y": t["y0"] + i * t["pitch"],
            "w": t["w"],
            "h": t["h"],
        }
    for i in range(4):
        sprites[f"oval_l_{i}"] = {
            "x": o["leftX"],
            "y": o["y0"] + i * o["pitch"],
            "w": o["w"],
            "h": o["h"],
        }
        sprites[f"oval_r_{i}"] = {
            "x": o["rightX"],
            "y": o["y0"] + i * o["pitch"],
            "w": o["w"],
            "h": o["h"],
        }
    for i in range(3):
        sprites[f"bottom_{i}"] = {
            "x": b["x"],
            "y": b["y0"] + i * b["pitch"],
            "w": b["w"],
            "h": b["h"],
        }
    return sprites


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description="KITT PNG → RGB565 atlas")
    parser.add_argument("--theme", default="kitt")
    parser.add_argument(
        "--input",
        type=Path,
        default=root / "themes" / "kitt" / "source" / "dashboard_ref.png",
    )
    args = parser.parse_args()

    theme_dir = root / "themes" / args.theme
    layout_path = theme_dir / "layout.json"
    if not args.input.is_file():
        raise SystemExit(f"Missing source PNG: {args.input}")
    layout = json.loads(layout_path.read_text(encoding="utf-8"))

    img = Image.open(args.input).convert("RGB")
    img = img.resize((CANVAS_W, CANVAS_H), Image.Resampling.LANCZOS)

    mx, my, mw, mh = modulator_bbox(layout)
    draw = ImageDraw.Draw(img)
    draw.rectangle([mx, my, mx + mw - 1, my + mh - 1], fill=(0, 0, 0))

    bin_data = bytearray(image_to_rgb565_bin(img))
    fix_path = Path(__file__).resolve().parent / "_atlas_fix_redbox.py"
    spec = importlib.util.spec_from_file_location("atlas_fix_redbox", fix_path)
    fix_mod = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(fix_mod)
    fixed = fix_mod.fix_auto_cruise_red(bin_data, layout)
    if fixed:
        print(f"auto-cruise red artifact: fixed {fixed} px")
    bin_data = bytes(bin_data)
    bin_path = theme_dir / "labels_atlas.bin"
    bin_path.write_bytes(bin_data)

    manifest = {
        "theme": args.theme,
        "format": "rgb565_le",
        "generated": True,
        "width": CANVAS_W,
        "height": CANVAS_H,
        "bin": bin_path.name,
        "sprites": sprite_rects(layout),
    }
    json_path = theme_dir / "labels_atlas.json"
    json_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")

    data_path = root / "firmware" / "agente-ia" / "data" / "kitt_labels_atlas.bin"
    data_path.parent.mkdir(parents=True, exist_ok=True)
    data_path.write_bytes(bin_data)

    print(f"Wrote {bin_path} ({len(bin_data)} bytes)")
    print(f"Wrote {json_path}")
    print(f"Wrote {data_path} (SPIFFS data/)")
    print(f"Modulator mask: x={mx} y={my} w={mw} h={mh}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""
Convierte un TTF/OTF a una cabecera de fuente compatible con LovyanGFX
(formato Adafruit GFX -> lgfx::GFXfont).

Pensado para los rótulos del tablero KITT: ASCII imprimible (0x20-0x7E),
con "negrita fuerte" por software (dilatación) para emular Eurostile / Microgramma
Bold Extended cuando solo se dispone de una alternativa libre (p.ej. Michroma).

Uso:
  python tools/ttf_to_gfx.py --ttf <archivo.ttf> --out <salida.h> \
      --name KittFontExtended --size 24 --bold 1 --boldv 1

Si en el futuro consigues con licencia "Microgramma D Extended.ttf", basta con:
  python tools/ttf_to_gfx.py --ttf "ruta/Microgramma D Extended.ttf" \
      --out firmware/agente-ia/font_kitt_extended.h --name KittFontExtended --size 24
y recompilar. No hay que tocar el firmware.
"""
import argparse
import os

import freetype


def build(ttf_path, size, first, last, bold, boldv, threshold):
    face = freetype.Face(ttf_path)
    face.set_pixel_sizes(0, size)
    y_advance = int(face.size.height) >> 6
    if y_advance <= 0:
        y_advance = size

    bitmap_bytes = []  # flujo de bytes concatenado
    glyphs = []        # (offset, w, h, xadv, xoff, yoff)

    for code in range(first, last + 1):
        face.load_char(chr(code), freetype.FT_LOAD_RENDER)
        g = face.glyph
        bmp = g.bitmap
        w, h, pitch = bmp.width, bmp.rows, bmp.pitch
        x_adv = g.advance.x >> 6
        x_off = g.bitmap_left
        y_off = 1 - g.bitmap_top  # convención Adafruit fontconvert

        # rejilla on/off (umbral sobre gris de 8 bits)
        grid = [[1 if bmp.buffer[y * pitch + x] >= threshold else 0
                 for x in range(w)] for y in range(h)]

        # negrita por software: dilatación a la derecha (bold) y hacia abajo (boldv)
        if w > 0 and h > 0 and (bold or boldv):
            nw, nh = w + bold, h + boldv
            dil = [[0] * nw for _ in range(nh)]
            for y in range(h):
                for x in range(w):
                    if grid[y][x]:
                        for dy in range(boldv + 1):
                            for dx in range(bold + 1):
                                dil[y + dy][x + dx] = 1
            grid = dil
            w, h = nw, nh
            x_adv += bold

        offset = len(bitmap_bytes)
        # empaquetado MSB-first, fila a fila, alineado a byte por glifo
        acc = 0
        nbits = 0
        for y in range(h):
            for x in range(w):
                acc = (acc << 1) | grid[y][x]
                nbits += 1
                if nbits == 8:
                    bitmap_bytes.append(acc)
                    acc, nbits = 0, 0
        if nbits:
            acc <<= (8 - nbits)
            bitmap_bytes.append(acc)

        glyphs.append((offset, w, h, x_adv, x_off, y_off))

    return bitmap_bytes, glyphs, y_advance


def emit(out_path, name, bitmap_bytes, glyphs, first, last, y_advance, src):
    guard = "FONT_" + name.upper() + "_H"
    lines = []
    lines.append("// Fuente generada por tools/ttf_to_gfx.py — NO editar a mano.")
    lines.append(f"// Origen: {os.path.basename(src)}")
    lines.append("// Formato: Adafruit GFX -> lgfx::GFXfont (LovyanGFX).")
    lines.append("#pragma once")
    lines.append("#include <LovyanGFX.hpp>")
    lines.append("")
    lines.append(f"static const uint8_t {name}Bitmaps[] = {{")
    row = "  "
    for i, b in enumerate(bitmap_bytes):
        row += f"0x{b:02X},"
        if len(row) >= 96:
            lines.append(row)
            row = "  "
    if row.strip():
        lines.append(row)
    lines.append("};")
    lines.append("")
    lines.append(f"static const lgfx::GFXglyph {name}Glyphs[] = {{")
    for idx, (off, w, h, xadv, xoff, yoff) in enumerate(glyphs):
        code = first + idx
        ch = chr(code) if 33 <= code <= 126 else " "
        lines.append(
            f"  {{ {off:5d}, {w:3d}, {h:3d}, {xadv:3d}, {xoff:4d}, {yoff:4d} }}, "
            f"// 0x{code:02X} '{ch}'"
        )
    lines.append("};")
    lines.append("")
    lines.append(
        f"static const lgfx::GFXfont {name}("
        f"(uint8_t*){name}Bitmaps, (lgfx::GFXglyph*){name}Glyphs, "
        f"0x{first:02X}, 0x{last:02X}, {y_advance});"
    )
    lines.append("")
    with open(out_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ttf", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--name", default="KittFontExtended")
    ap.add_argument("--size", type=int, default=24)
    ap.add_argument("--first", type=lambda s: int(s, 0), default=0x20)
    ap.add_argument("--last", type=lambda s: int(s, 0), default=0x7E)
    ap.add_argument("--bold", type=int, default=1, help="dilatación horizontal (px)")
    ap.add_argument("--boldv", type=int, default=1, help="dilatación vertical (px)")
    ap.add_argument("--threshold", type=int, default=128)
    args = ap.parse_args()

    bitmap_bytes, glyphs, y_advance = build(
        args.ttf, args.size, args.first, args.last,
        args.bold, args.boldv, args.threshold)
    emit(args.out, args.name, bitmap_bytes, glyphs,
         args.first, args.last, y_advance, args.ttf)

    print(f"OK -> {args.out}")
    print(f"  glifos: {len(glyphs)}  bytes bitmap: {len(bitmap_bytes)}  "
          f"yAdvance: {y_advance}")


if __name__ == "__main__":
    main()

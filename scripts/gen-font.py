#!/usr/bin/env python3
# Copyright (C) 2026  Jonathan Pepin
# SPDX-License-Identifier: GPL-3.0-or-later
"""Turn the vendored BDF faces into the firmware's font tables.

BDF is plain text, so this needs no rasteriser and no build dependency — which
is why a bitmap Helvetica was chosen over rasterising a vector face: at the
sizes a boot menu uses, a face someone fitted to the pixel grid by hand beats
anything an unhinted rasteriser produces, and it costs nothing to read.

Each face becomes one cell-aligned bitmap: every glyph is placed against the
common baseline (FONT_ASCENT), so two sizes can share a line and two weights can
sit side by side. The advance comes from DWIDTH and the left bearing from the
bounding box, which is what makes the spacing the designer's rather than ours.

The faces are emitted in ascending cell height as a ladder, so that drawing at
the display's own resolution can pick the size it actually needs instead of
magnifying one.

  usage: gen-font.py <output .cpp> <file.bdf> [<file.bdf> ...]
"""
import re
import sys

FIRST, LAST = 0x20, 0xFF

# Beyond Latin-1: what a French translation reaches for and ISO-8859-1 lacks.
# Kept in this order; the C side searches the list, all nine of it.
EXTRA = [0x0152, 0x0153, 0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2026]


class Glyph:
    __slots__ = ("advance", "rows", "left", "top", "width", "height")


def read_bdf(path):
    ascent = descent = None
    glyphs = {}
    code = advance = None
    bbx = None
    bitmap = None

    with open(path, encoding="latin-1") as f:
        for line in f:
            line = line.rstrip("\n")
            if bitmap is not None:
                if line.startswith("ENDCHAR"):
                    g = Glyph()
                    g.advance = advance
                    g.width, g.height, g.left, g.top = bbx
                    g.rows = bitmap
                    glyphs[code] = g
                    bitmap = None
                else:
                    bitmap.append(int(line, 16) if line else 0)
                continue

            if line.startswith("FONT_ASCENT"):
                ascent = int(line.split()[1])
            elif line.startswith("FONT_DESCENT"):
                descent = int(line.split()[1])
            elif line.startswith("ENCODING"):
                code = int(line.split()[1])
            elif line.startswith("DWIDTH"):
                advance = int(line.split()[1])
            elif line.startswith("BBX"):
                w, h, xoff, yoff = (int(v) for v in line.split()[1:5])
                bbx = (w, h, xoff, yoff)
            elif line.startswith("BITMAP"):
                bitmap = []

    if ascent is None or descent is None:
        raise SystemExit(f"{path}: no FONT_ASCENT/FONT_DESCENT")
    return ascent, descent, glyphs


def pack(path):
    declared_ascent, declared_descent, glyphs = read_bdf(path)

    codes = list(range(FIRST, LAST + 1)) + EXTRA

    # The cell comes from the glyphs, not from FONT_ASCENT. Helvetica declares an
    # ascent of 11 at twelve pixels and then draws É twelve rows above the
    # baseline: trusting the declaration drops the top row of every accented
    # capital, which shows up as an acute that has lost half its stroke.
    ascent, descent = declared_ascent, declared_descent
    for code in codes:
        g = glyphs.get(code)
        if g is None:
            continue
        ascent = max(ascent, g.top + g.height)
        descent = max(descent, -g.top)
    height = ascent + descent
    max_right = 1
    for code in codes:
        g = glyphs.get(code)
        if g is not None:
            max_right = max(max_right, g.left + g.width, g.advance)
    stride = (max_right + 7) // 8

    bits, widths = [], []
    for code in codes:
        g = glyphs.get(code)
        cell = [0] * height
        if g is None:
            widths.append(0)
            bits.extend([0] * (height * stride))
            continue

        # BDF rows are padded to whole bytes for the glyph's own width; the cell
        # is padded to the face's width. Shifting by the left bearing is what
        # keeps the spacing the designer's and not ours.
        row_bytes = (g.width + 7) // 8
        top = ascent - (g.top + g.height)
        for i, value in enumerate(g.rows):
            y = top + i
            if y < 0 or y >= height:
                continue                # a glyph taller than the declared cell
            value >>= (row_bytes * 8 - g.width)     # right-align to its own width
            value <<= (stride * 8 - g.width - g.left)
            cell[y] = value & ((1 << (stride * 8)) - 1)

        for y in range(height):
            for b in range(stride):
                bits.append((cell[y] >> ((stride - 1 - b) * 8)) & 0xFF)
        widths.append(g.advance)

    # The cap height, measured on the H rather than taken from the property:
    # text is centred on its capitals, not on a cell that carries a descent no
    # capital uses. Centring on the cell is what makes a label sit high.
    cap = glyphs[ord("H")].height if ord("H") in glyphs else ascent
    return height, ascent, cap, stride, bits, widths


def emit(out, name, path):
    height, ascent, cap, stride, bits, widths = pack(path)

    out.write(f"// {name}: cell {height}, ascent {ascent}, cap {cap}, "
              f"{stride} byte(s) per row, from {path}\n")
    out.write(f"static const unsigned char s_{name}Bits[] =\n{{\n")
    for i in range(0, len(bits), 16):
        out.write("\t" + ", ".join(f"0x{b:02X}" for b in bits[i:i + 16]) + ",\n")
    out.write("};\n\n")
    out.write(f"static const unsigned char s_{name}Width[] =\n{{\n")
    for i in range(0, len(widths), 24):
        out.write("\t" + ", ".join(str(w) for w in widths[i:i + 24]) + ",\n")
    out.write("};\n\n")
    out.write(f"const TOkapiaFont {name} =\n{{\n")
    out.write(f"\t{height}, {ascent}, {cap}, {FIRST}, {LAST}, {stride},\n")
    out.write(f"\ts_{name}Bits, s_{name}Width,\n")
    out.write(f"\t{len(EXTRA)}, s_OkapiaFontExtra\n}};\n\n")
    return height


def face_name(path):
    m = re.search(r"helv([RB])(\d+)-(\d+)\.bdf$", path)
    if m is None:
        raise SystemExit(f"cannot name a face from {path}")
    weight = "Bold" if m.group(1) == "B" else "Regular"
    key = f"{m.group(2)}x{m.group(3)}"
    return f"OkapiaHelv{weight}{key}", m.group(1), key


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    out_path, sources = sys.argv[1], sys.argv[2:]

    with open(out_path, "w", encoding="utf-8") as out:
        out.write("// Generated by scripts/gen-font.py — do not edit.\n")
        out.write("//\n// X11 Adobe Helvetica, vendored in assets/fonts/ with its COPYING.\n\n")
        out.write('#include "okapia_font.h"\n\n')
        out.write("static const unsigned short s_OkapiaFontExtra[] =\n{\n\t")
        out.write(", ".join(f"0x{c:04X}" for c in EXTRA))
        out.write("\n};\n\n")

        # Pairs are keyed by the source face, never by the cell height: a bold
        # face is often a row taller than its regular, and pairing by height
        # silently splits the two into separate rungs with a weight missing
        # from each.
        faces = {}
        for path in sources:
            name, weight, key = face_name(path)
            height = emit(out, name, path)
            entry = faces.setdefault(key, {})
            entry[weight] = name
            if weight == "R":
                entry["height"] = height

        out.write("const TOkapiaFontSet OkapiaFaces[] =\n{\n")
        count = 0
        for key in sorted(faces, key=lambda k: faces[k]["height"]):
            pair = faces[key]
            if "R" not in pair or "B" not in pair:
                raise SystemExit(f"{key}: only one weight given")
            out.write(f"\t{{ {pair['height']}, &{pair['R']}, &{pair['B']} }},\n")
            count += 1
        out.write("};\n\n")
        out.write(f"const unsigned OkapiaFaceCount = {count};\n")


main()

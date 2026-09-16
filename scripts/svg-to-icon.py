#!/usr/bin/env python3
# Copyright (C) 2026  Jonathan Pepin
# SPDX-License-Identifier: GPL-3.0-or-later
"""Turn one of Okapia's own icon drawings into the LVGL descriptor the build reads.

The system folder icons come from Apple artwork, captured as PNG and re-packed
by boot-menu/generate-icons.py. Okapia's own marks have no such source: they are
drawn here, so the drawing is the source and this is what reduces it.

  usage: svg-to-icon.py <output .c> <name> <width> <height> <source .svg>

The dialect is deliberately small — <rect>, <circle> and <line>, with fill and
stroke — because a general SVG reader is a dependency and this repository builds
with nothing but a toolchain and Python. Anything outside it raises rather than
being quietly ignored: an icon that silently lost a line would be found on a
screen, months later, by somebody who did not draw it.

Sampling is 16x16 per output pixel and the threshold is coverage, not intensity:
below a certain ink a pixel is white. Drawn for 25 pixels, a two-unit stroke on
a hundred-unit drawing is half a pixel, so where the threshold sits decides
whether a circle survives at all — hence a flag rather than a constant, and the
value used is recorded in the generated file.
"""
import math
import re
import sys

SAMPLES = 16            # per axis, per output pixel
THRESHOLD = 0.32        # ink coverage at or above which a pixel is black


class Shape:
    """One drawn element, as a coverage test in the drawing's own units."""

    def __init__(self, kind, **kw):
        self.kind = kind
        self.__dict__.update(kw)

    def hits(self, x, y):
        if self.kind == "rect":
            return (self.x <= x <= self.x + self.w
                    and self.y <= y <= self.y + self.h)
        if self.kind == "disc":
            return math.hypot(x - self.cx, y - self.cy) <= self.r
        if self.kind == "ring":
            return abs(math.hypot(x - self.cx, y - self.cy) - self.r) <= self.t / 2
        if self.kind == "line":
            dx, dy = self.x2 - self.x1, self.y2 - self.y1
            n2 = dx * dx + dy * dy
            if n2 == 0:
                return False
            u = ((x - self.x1) * dx + (y - self.y1) * dy) / n2
            if u < 0 or u > 1:
                return False
            px, py = self.x1 + dx * u, self.y1 + dy * u
            return math.hypot(x - px, y - py) <= self.t / 2
        raise ValueError(self.kind)


def attrs(tag):
    return dict(re.findall(r'([\w-]+)\s*=\s*"([^"]*)"', tag))


def number(value, what):
    try:
        return float(value)
    except (TypeError, ValueError):
        raise SystemExit(f"{what}: not a number: {value!r}")


def is_ink(colour):
    """Black is ink, white is not, and nothing else is allowed: a grey in a
    drawing meant for one bit is a decision nobody made on purpose."""
    if colour in (None, "none"):
        return None
    c = colour.strip().lower()
    if c in ("#000", "#000000", "black"):
        return True
    if c in ("#fff", "#ffffff", "white"):
        return False
    raise SystemExit(f"{c}: a one-bit drawing takes black, white or none")


def parse(path):
    src = open(path, encoding="utf-8").read()

    box = re.search(r'viewBox\s*=\s*"([^"]*)"', src)
    if not box:
        raise SystemExit(f"{path}: no viewBox")
    vx, vy, vw, vh = (float(v) for v in box.group(1).split())

    shapes = []
    for kind, rest in re.findall(r"<(rect|circle|line)\b([^>]*)>", src):
        a = attrs(rest)
        fill = is_ink(a.get("fill"))
        stroke = is_ink(a.get("stroke"))
        width = number(a.get("stroke-width", 0), f"{kind} stroke-width")

        # Painted in the order they are written, exactly as a renderer would:
        # a white fill under a black stroke is what hollows the hub out.
        if kind == "rect":
            if fill is not None:
                shapes.append((fill, Shape("rect", x=number(a["x"], "rect x"),
                                           y=number(a["y"], "rect y"),
                                           w=number(a["width"], "rect width"),
                                           h=number(a["height"], "rect height"))))
        elif kind == "circle":
            cx = number(a["cx"], "circle cx")
            cy = number(a["cy"], "circle cy")
            r = number(a["r"], "circle r")
            if fill is not None:
                shapes.append((fill, Shape("disc", cx=cx, cy=cy, r=r)))
            if stroke is not None and width > 0:
                shapes.append((stroke, Shape("ring", cx=cx, cy=cy, r=r, t=width)))
        else:
            if stroke is None or width <= 0:
                raise SystemExit(f"{path}: a line with no stroke draws nothing")
            shapes.append((stroke, Shape("line", x1=number(a["x1"], "line x1"),
                                         y1=number(a["y1"], "line y1"),
                                         x2=number(a["x2"], "line x2"),
                                         y2=number(a["y2"], "line y2"),
                                         t=width)))
    if not shapes:
        raise SystemExit(f"{path}: nothing to draw")
    return (vx, vy, vw, vh), shapes


def rasterise(view, shapes, width, height, threshold):
    """The drawing is square and the box may not be, so it is fitted and centred
    rather than stretched: a disc in an oblong icon is an oblong disc."""
    vx, vy, vw, vh = view
    scale = min(width / vw, height / vh)
    ox = (width - vw * scale) / 2
    oy = (height - vh * scale) / 2

    rows = []
    for py in range(height):
        row = []
        for px in range(width):
            ink = 0
            for sy in range(SAMPLES):
                y = vy + (py + (sy + 0.5) / SAMPLES - oy) / scale
                for sx in range(SAMPLES):
                    x = vx + (px + (sx + 0.5) / SAMPLES - ox) / scale
                    value = 0
                    for black, shape in shapes:
                        if shape.hits(x, y):
                            value = 1 if black else 0
                    ink += value
            row.append(1 if ink >= threshold * SAMPLES * SAMPLES else 0)
        rows.append(row)
    return rows


def bitmap_for(rows):
    width = len(rows[0])
    stride = (width + 7) // 8
    data = bytearray()
    for row in rows:
        packed = bytearray(stride)
        for x, pixel in enumerate(row):
            if pixel:
                packed[x // 8] |= 1 << (7 - x % 8)
        data.extend(packed)
    return stride, bytes(data)


def format_bytes(data, stride=None):
    step = stride or 16
    return "\n".join(
        "    " + ", ".join(f"0x{v:02x}" for v in data[i:i + step]) + ","
        for i in range(0, len(data), step))


def write(out, name, rows, source, threshold):
    """The same shape boot-menu/generate-icons.py writes, so scripts/gen-icons.py
    reads this file without knowing which of the two produced it."""
    width, height = len(rows[0]), len(rows)
    stride, bitmap = bitmap_for(rows)
    art = "\n".join(" * " + "".join("#" if p else "." for p in row) for row in rows)

    body = f"""/* Generated by scripts/svg-to-icon.py from {source} — do not edit.
 *
 * Coverage threshold {threshold}. What it produced, so a change shows up in a
 * diff as a picture rather than as a wall of hexadecimal:
 *
{art}
 */
#include "system-icons.h"

static const LV_ATTRIBUTE_MEM_ALIGN uint8_t okapia_{name}_icon_map[] = {{
{format_bytes(bytes((0, 0, 0, 0)) + bytes((0, 0, 0, 0xff)))}

{format_bytes(bitmap, stride)}
}};

const lv_image_dsc_t okapia_{name}_icon = {{
    .header = {{
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_I1,
        .flags = 0,
        .w = {width},
        .h = {height},
        .stride = {stride},
        .reserved_2 = 0,
    }},
    .data_size = sizeof(okapia_{name}_icon_map),
    .data = okapia_{name}_icon_map,
    .reserved = NULL,
    .reserved_2 = NULL,
}};
"""
    open(out, "w", encoding="utf-8").write(body)
    print(art.replace(" * ", ""))
    print(f"{out}: {name}, {width}x{height}")


def main(argv):
    if len(argv) not in (6, 7):
        raise SystemExit(__doc__)
    out, name, width, height, source = argv[1:6]
    threshold = float(argv[6]) if len(argv) == 7 else THRESHOLD
    view, shapes = parse(source)
    rows = rasterise(view, shapes, int(width), int(height), threshold)
    write(out, name, rows, source, threshold)


if __name__ == "__main__":
    main(sys.argv)

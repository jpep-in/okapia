#!/usr/bin/env python3
"""Keep only the characters the firmware can show, and drop the rest of a BDF.

The X11 Helvetica faces carry the whole of ISO 10646 — 756 glyphs, most of a
hundred kilobytes each, nearly all of it Cyrillic, Greek and mathematics the
boot menu will never print. Vendoring them whole would put a megabyte and a half
of dead weight in the repository for no gain, and the trimmed files are still
plain BDF that any tool can read.

Kept: Latin-1 from space to 0xFF, plus the handful of typographic marks a French
translation reaches for and ISO-8859-1 lacks.

  usage: trim-bdf.py <source.bdf> <destination.bdf>
"""
import sys

KEEP = set(range(0x20, 0x100)) | {
    0x0152, 0x0153,                     # Œ œ
    0x2013, 0x2014,                     # – —
    0x2018, 0x2019, 0x201C, 0x201D,     # ‘ ’ “ ”
    0x2026,                             # …
}


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    src, dst = sys.argv[1], sys.argv[2]

    lines = open(src, encoding="latin-1").read().split("\n")
    head, blocks, current, encoding = [], [], None, None

    for line in lines:
        if line.startswith("STARTCHAR"):
            current, encoding = [line], None
            continue
        if current is None:
            if line.startswith("CHARS "):
                head.append("@@CHARS@@")   # count is only known at the end
            elif line.strip() == "ENDFONT":
                pass                        # re-emitted after the blocks
            else:
                head.append(line)
            continue
        current.append(line)
        if line.startswith("ENCODING"):
            encoding = int(line.split()[1])
        elif line.startswith("ENDCHAR"):
            if encoding in KEEP:
                blocks.append(current)
            current = None

    if not blocks:
        raise SystemExit(f"{src}: nothing kept — is it a BDF?")

    out = []
    for line in head:
        out.append(f"CHARS {len(blocks)}" if line == "@@CHARS@@" else line)
    for block in blocks:
        out.extend(block)
    out.append("ENDFONT")
    out.append("")

    open(dst, "w", encoding="latin-1").write("\n".join(out))


main()

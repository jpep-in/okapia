#!/usr/bin/env python3
"""Check a Macintosh ROM before Okapia ever tries to boot it.

Basilisk II accepts a ROM only if the 16-bit big-endian word at offset 8 is
0x067C, meaning 32-bit clean — mandatory in DIRECT_ADDRESSING (rom_patches.cpp:838).
The stored checksum is the big-endian longword at offset 0, and it must equal the
sum of every 16-bit word from offset 4 onwards, truncated to 32 bits.

Copyright (C) 2026  Okapia contributors
SPDX-License-Identifier: GPL-3.0-or-later
"""
import sys
from pathlib import Path

ROM_VERSIONS = {
    0x0000: ("64K", "original Macintosh"),
    0x0075: ("Plus", "Mac Plus"),
    0x0276: ("Classic", "SE / Classic"),
    0x0178: ("Mac II", "not 32-bit clean"),
    0x067C: ("32-bit clean", "Mac II class and later, Quadra included"),
}


def check(path: Path) -> bool:
    data = path.read_bytes()
    size = len(data)
    print(f"\n{path.name}")
    print(f"  size            {size} bytes ({size // 1024} KB)")

    if size < 16:
        print("  VERDICT         too small to be a ROM")
        return False

    stored = int.from_bytes(data[0:4], "big")
    computed = sum(int.from_bytes(data[i:i + 2], "big")
                   for i in range(4, size, 2)) & 0xFFFFFFFF
    ok_sum = stored == computed
    print(f"  checksum        stored 0x{stored:08X}, computed 0x{computed:08X}"
          f"  {'match' if ok_sum else 'MISMATCH'}")

    version = int.from_bytes(data[8:10], "big")
    name, note = ROM_VERSIONS.get(version, ("unknown", "not recognised by Basilisk II"))
    print(f"  version word    0x{version:04X}  {name} ({note})")

    usable = version == 0x067C
    if usable and size not in (512 * 1024, 1024 * 1024):
        print(f"  note            unusual size for a 32-bit clean ROM")

    print(f"  VERDICT         {'usable by Okapia' if usable else 'NOT usable: DIRECT_ADDRESSING needs a 32-bit clean ROM'}")
    if usable and not ok_sum:
        print("                  checksum mismatch: image may be altered or byte-swapped")
    return usable and ok_sum


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        print("usage: check-rom.py <rom-file> [...]", file=sys.stderr)
        return 2
    results = [check(Path(a)) for a in argv[1:]]
    print()
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))

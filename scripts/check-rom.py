#!/usr/bin/env python3
"""Check a Macintosh ROM before Okapia ever tries to boot it.

Two families, two sets of rules, and nothing in common between them.

68k, for Basilisk II: the ROM is accepted only if the 16-bit big-endian word at
offset 8 is 0x067C, meaning 32-bit clean — mandatory in DIRECT_ADDRESSING
(BasiliskII/src/rom_patches.cpp:838). The stored checksum is the big-endian
longword at offset 0, and it must equal the sum of every 16-bit word from offset
4 onwards, truncated to 32 bits.

PowerPC, for SheepShaver: neither of those is even looked at — the version word
and the checksum are printed and never tested
(SheepShaver/src/rom_patches.cpp:674-678). What decides is the nanokernel's name
at offset 0x30d064, which must be one of six (:682-694); anything else makes
PatchROM() fail and the Mac never starts. The image is 4 MB exactly, or a
<CHRP-BOOT> file this script decompresses the way DecodeROM() does (:148-199) —
which is why a "Mac OS ROM" file taken from a System Folder can be checked, and
used, as it stands.

And one consequence worth knowing before hunting for a boot failure: a NewWorld
ROM refuses every System older than 8.1, in so many words
(SheepShaver/src/emul_op.cpp:426-434).

Copyright (C) 2026  Okapia contributors
SPDX-License-Identifier: GPL-3.0-or-later
"""
import re
import sys
from pathlib import Path

ROM_VERSIONS = {
    0x0000: ("64K", "original Macintosh"),
    0x0075: ("Plus", "Mac Plus"),
    0x0276: ("Classic", "SE / Classic"),
    0x0178: ("Mac II", "not 32-bit clean"),
    0x067C: ("32-bit clean", "Mac II class and later, Quadra included"),
}

PPC_ROM_SIZE = 4 * 1024 * 1024
NANOKERNEL_ID = 0x30D064

# The six names SheepShaver knows, and the oldest System each one allows. The
# 8.1 floor is NewWorld's alone and it is enforced by the emulator, not by taste.
NANOKERNELS = [
    (b"Boot TNT",       "TNT",       "7.5.2"),
    (b"Boot Alchemy",   "Alchemy",   "7.5.2"),
    (b"Boot Zanzibar",  "Zanzibar",  "7.5.2"),
    (b"Boot Gazelle",   "Gazelle",   "7.5.2"),
    (b"Boot Gossamer",  "Gossamer",  "7.5.2"),
    (b"NewWorld",       "NewWorld",  "8.1"),
]


def decode_lzss(src: bytes, size: int) -> bytearray:
    """Port of SheepShaver's decode_lzss (rom_patches.cpp:79-119)."""
    dest = bytearray()
    dictionary = bytearray(0x1000)
    run_mask, dict_idx, pos = 0, 0xFEE, 0
    while True:
        if run_mask < 0x100:
            size -= 1
            if size < 0 or pos >= len(src):
                break
            run_mask = src[pos] | 0xFF00
            pos += 1
        bit = run_mask & 1
        run_mask >>= 1
        if bit:
            size -= 1
            if size < 0 or pos >= len(src):
                break
            c = src[pos]
            pos += 1
            dictionary[dict_idx] = c
            dict_idx = (dict_idx + 1) & 0xFFF
            dest.append(c)
        else:
            size -= 1
            if size < 0 or pos >= len(src):
                break
            idx = src[pos]
            pos += 1
            size -= 1
            if size < 0 or pos >= len(src):
                break
            cnt = src[pos]
            pos += 1
            idx |= (cnt << 4) & 0xF00
            cnt = (cnt & 0x0F) + 3
            for _ in range(cnt):
                c = dictionary[idx]
                idx = (idx + 1) & 0xFFF
                dictionary[dict_idx] = c
                dict_idx = (dict_idx + 1) & 0xFFF
                dest.append(c)
    return dest


def decode_parcels(src: bytes) -> bytearray:
    """Port of SheepShaver's decode_parcels (rom_patches.cpp:123-142).

    Mac OS 9.x ROMs are a chain of parcels; only the one typed 'rom ' carries
    the image, and it is LZSS inside.
    """
    dest = bytearray()
    offset = 0x14
    while offset != 0 and offset + 28 <= len(src):
        words = [int.from_bytes(src[offset + 4 * i:offset + 4 * i + 4], "big")
                 for i in range(3)]
        next_offset, parcel_type, lzss_offset = words
        if parcel_type == 0x726F6D20:            # 'rom '
            start = offset + lzss_offset
            dest = decode_lzss(src[start:], next_offset - start)
        offset = next_offset
    return dest


def decode_ppc_rom(data: bytes):
    """The 4 MB image, and how it was obtained — or (None, reason)."""
    if len(data) == PPC_ROM_SIZE:
        return data, "plain 4 MB image"
    if not data.startswith(b"<CHRP-BOOT>"):
        return None, "neither 4 MB nor a <CHRP-BOOT> file"

    # The offsets are written in the Forth header as six hex digits ending seven
    # bytes before the name, which is how DecodeROM() reads them back.
    def constant(name):
        m = re.search(br"([0-9a-fA-F]{6})\s+constant\s+" + name, data)
        return int(m.group(1), 16) if m else None

    lzss_off, lzss_size = constant(b"lzss-offset"), constant(b"lzss-size")
    if lzss_off is None:
        lzss_off, lzss_size = constant(b"parcels-offset"), constant(b"parcels-size")
        if lzss_off is None:
            return None, "<CHRP-BOOT> without lzss or parcels constants"

    if data[lzss_off:lzss_off + 4] == b"prcl":
        return decode_parcels(data[lzss_off:lzss_off + lzss_size]), "parcels"
    return decode_lzss(data[lzss_off:], lzss_size), "LZSS"


def check_powerpc(data: bytes) -> bool:
    image, how = decode_ppc_rom(data)
    if image is None:
        print(f"  VERDICT         NOT a PowerPC ROM either: {how}")
        return False

    print(f"  decoded         {how}, {len(image)} bytes")
    if len(image) < NANOKERNEL_ID + 16:
        print("  VERDICT         decoded image too short to hold a nanokernel name")
        return False

    name = bytes(image[NANOKERNEL_ID:NANOKERNEL_ID + 16])
    # The name is not terminated: it runs straight into the next field, so it is
    # cut at the first byte that is not printable text rather than at a NUL.
    printable = "".join(chr(b) for b in name.split(b"\x00")[0]
                        if 0x20 <= b < 0x7F)
    print(f"  nanokernel      \"{printable}\"  (offset 0x{NANOKERNEL_ID:x})")

    for prefix, kind, floor in NANOKERNELS:
        if name.startswith(prefix):
            print(f"  ROM type        {kind}")
            print(f"  Mac OS          {floor} to 9.0.4")
            if kind == "NewWorld":
                print("  note            refuses every System older than 8.1")
            print("  VERDICT         usable by SheepShaver")
            return True

    print("  VERDICT         NOT usable: SheepShaver knows no such nanokernel")
    return False


def check(path: Path) -> bool:
    data = path.read_bytes()
    size = len(data)
    print(f"\n{path.name}")
    print(f"  size            {size} bytes ({size // 1024} KB)")

    if size < 16:
        print("  VERDICT         too small to be a ROM")
        return False

    # The two families share nothing — not the checksum, not the version word —
    # so the routing has to come before either test. No 68k ROM is 4 MB: they
    # stop at 1 MB.
    if size == PPC_ROM_SIZE or data.startswith(b"<CHRP-BOOT>"):
        print("  family          PowerPC (SheepShaver)")
        return check_powerpc(data)
    print("  family          68k (Basilisk II)")

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

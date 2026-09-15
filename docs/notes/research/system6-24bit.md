# System 6 in 24-bit addressing

> Technical study and implementation plan, 2026-09-11. **Nothing implemented yet.** Goal: start System 6 on the 68k
> engine with no new setting — choosing the volume is enough.

## Proposed decision

```text
System 6 detected  -> Basilisk "banks", 24-bit addressing
System 7 or 8 68k  -> Basilisk "direct", 32-bit addressing (today's engine, untouched)
PowerPC System     -> SheepShaver
```

No new machine, ROM or 24/32-bit setting. Both 68k builds present the same Macintosh IIci — the same 32-bit-clean ROM
(version word `$067C`), `modelid 5`, `cpu 3` (68030), FPU on (upstream's IIci values, `prefs_items.cpp:56`, `:106-109`).
Only the memory engine and the visible RAM change: **8 MB** for System 6. System 6.0.8 is the first target (a IIci
accepts 6.0.4 or later). The direct engine gets no mode test and no indirection in its CPU loop.

## What 24-bit means

The 68030 keeps 32-bit registers and pointers; in 24-bit mode only the low 24 bits decode, and the high byte may carry
flags that old ROMs, applications, INITs and drivers used. The IIci's useful map:

```text
$000000-$7FFFFF  guest RAM, 8 MB at most
$800000-$8FFFFF  ROM window
$900000-$EFFFFF  standard NuBus windows, 1 MB per slot
$F00000-$FFFFFF  I/O
```

So the limit is **8 MB of physical RAM**, not 4 (16 MB is the whole logical space). Inside Macintosh's 14 MB belongs to
System 7's virtual memory reusing empty NuBus windows — neither physical RAM nor available to System 6.

## Why "banks"

`BasiliskII/TECH:43-72`: the 4 GB 68k space is cut into 64 KB banks; `mem_banks[65536]` holds each bank's read, write and
translate functions. In the old `uae_cpu`, `memory_init()` builds a 24-bit map when `TwentyFourBitAddressing` is set —
`ram24`/`rom24` banks, the low map repeated for every high byte (`uae_cpu/memory.cpp:625-638`), accessors masking with
`0xffffff` (`:184-238`, `:291-320`). Banks can do 32-bit too; Okapia would use it only for System 6.

Cost: each guest access computes a bank index, reads `mem_banks`, calls indirectly and translates, where direct
addressing adds `MEMBaseDiff` inline. Slower in both modes; to be measured, no figure announced before. With
`SAVE_MEMORY_BANKS` the table is 65,536 pointers, 512 KB of BSS, built once.

**Not a run-time switch in one core**: accessors are chosen by the preprocessor and inlined into opcode handlers, so a
switch would mean a test in nearly every access (a permanent System 7/8 regression) or two handler sets in one core. Two
separately compiled Basilisk instances give the same result without touching the fast path.

## State of the code

- Okapia builds only direct: `ENGINE_DEFINES = -DDIRECT_ADDRESSING -DFPU_IEEE`. Upstream's
  `--enable-addressing=banks` means neither `DIRECT_ADDRESSING` nor `REAL_ADDRESSING` (`configure.ac:1457-1507`).
- **`uae_cpu_2021` has no banks backend**: `memory.cpp` is `// dummy`, `memory.h:58-140` is direct only, though
  `basilisk_glue.cpp:83-101` still calls `memory_init()` on the banked branch. It must be ported from `uae_cpu`, as a
  patch, without going back to the old core.
- **24/32-bit is inferred from the ROM**: `main.cpp:90-96` forces `TwentyFourBitAddressing = false` for a `$067C` ROM,
  although "32-bit clean" does not forbid a IIci from starting System 6 in 24-bit, and `CheckROM()` already accepts that
  ROM in a banked build (`rom_patches.cpp:831-842`). The decision must come from the selected System, set before
  `Init680x0()`/`memory_init()` and constant for the session.
- **The Circle port assumes direct addressing**: `main_circle.cpp` stops any other build with
  `#error "Okapia builds with DIRECT_ADDRESSING"`; the 68k video publishes `Host2MacAddr(s_pMacPixels)` and does not fill
  `MacFrameBaseHost`, `MacFrameSize`, `MacFrameLayout`, which banks needs; `get_virtual_address()` is absent from the
  banked backend.

## Detecting System 6

The inventory already reads the System's `vers` resource. Mounting `qemu/boot608.hda` read-only with the same libhfs
corrected an earlier assumption:

```text
HFS partition 1, volume "System 6.0.8", blessed folder CNID 18
System: type ZSYS, creator MACS, data 860, resource 588416, 'boot' present, 'vers' 1 and 2 = 6.0.8
```

The System file **does** have `vers`; detection fails because Okapia requires type `zsys` and System 6 says `ZSYS`
(the comment in `tests/host/check_platform.cpp` blaming a missing `vers` describes the symptom, not the cause). Accepting
`ZSYS` alone is not enough: the 6.0.8 System Folder holds four `ZSYS/MACS` files (Backgrounder, MultiFinder, Scrapbook
File, System), and Backgrounder's `vers` says 1.3.

**Rule**: in the blessed folder, files with creator `MACS` and type exactly `zsys` or `ZSYS` (HFS types are
case-sensitive; accept both known codes rather than compare case-insensitively), whose resource fork contains a `boot`
resource (present in System 6.0.8 and in the 7.1 reference); then read `vers`. Otherwise `Unknown` — never System 6 by
default, never from a file, volume or image name, and no Finder fallback (a second convention and a Finder version is
not the System's). A generation enum separate from the CPU flavour: `Unknown | System6 | System7Or8 | PowerPC`. The
boot menu's information page shows the result ("Basilisk, 24-bit, 8 MB"); there is nothing to choose.

## Architecture to implement

- **A third engine** `ENGINE=basilisk-banks`: `-DOKAPIA_ADDRESSING_BANKS=1 -DFPU_IEEE`, in its own `emu-basilisk-banks/`
  (objects do not depend on the Makefile, so a shared directory would keep objects built with the wrong `-D`); partial
  link with its wraps, rename every defined symbol with `banks__` except a C entry point `OkapiaRun68kBanks`; add it to
  `okapia-clean`.
- **Banks backend** ported from `uae_cpu/memory.{h,cpp}` into a patch: `addrbank`, `mem_banks`, RAM, ROM, frame buffer
  and invalid banks, `memory_init()`, `map_banks()`, the 2021 core's interfaces and signatures. The direct path must
  compile to the same code. First milestone: compile and map tests, no ROM.
- **Memory map**: RAM at 0, 8 MB; ROM window at `$00800000`; low banks repeated for every high byte, so `$00123456` and
  `$AB123456` are one byte and `$408xxxxx` aliases the ROM window. The old `memory_init()` already caps RAM below the
  ROM (`uae_cpu/memory.cpp:584-604`). Check: reset vectors and absolute ROM addresses, subtractions from `ROMBaseMac` in
  patches and `video.cpp`, a 512 KB ROM mirrored in its 1 MB window, accesses straddling two banks, holes on
  `dummy_bank`.
- **Host RAM**: always claim the maximum block (256 MB plus overhead) even for System 6, since `MacRamClaim()` reuses a
  first block for smaller requests but cannot grow it — a later switch to direct at 256 MB would fail.
- **Video**: a `$067C` ROM already calls `VideoInit(false)` (`main.cpp:173`), so the virtual NuBus card stays; 24-bit
  imposes neither 512×342 nor monochrome, only a guest frame buffer address. Proposal: `$E00000`, a 1 MB aperture, a
  frame buffer bank masking the high byte, subtracting `$E00000`, reaching `s_pMacPixels`, repeated in the aliases,
  refusing overflow; fill `MacFrameBaseHost`/`Size`/`Layout`. Filter modes on `bytes_per_row × height` against the
  aperture: 640×480×8, ×16, 800×600×16, 1024×768×8 and 1152×870×8 fit; 640×480×32 and 1024×768×16 do not.
- **Switching**: `OkapiaSwitchTo68kDirect`, `OkapiaSwitchTo68kBanks`, `OkapiaSwitchToPowerPC`. The direct engine stays the
  entry point (board, inventory, firmware) and hands over before starting the CPU; after a restart any engine can be
  chosen.
- **PRAM and preferences**: both Basilisk builds are the same IIci and may share `/BasiliskII_XPRAM`; no new preference;
  the memory menu stays System 7/8's and the information page says 8 MB effective.
- **Kernel size**: the two-engine image already neared the 4 MB reservation; a second 68k core plus 512 KB of BSS would
  exceed it (the check is on `_end`, BSS included, `sysinit.cpp:337-343`). Raise `--kernel-max-size` to 8 MB through
  `build-libs.sh` for every target (`memorymap.h:46-79` moves stacks, page tables and heap accordingly).

## Plan

- **A. Detection** — `zsys|ZSYS` + `MACS` + `boot`, then `vers`; `System6` generation; tests on both cases, several
  `ZSYS/MACS` files and no evidence at all. *Done so far: inspection of `boot608.hda`, the `ZSYS`/`zsys` difference.*
- **B. Banks backend for 2021** — patch, separate build, `FPU_IEEE` and `EXCEPTIONS_VIA_LONGJMP` kept, direct path
  unchanged, map unit tests.
- **C. 24-bit IIci start** — `TwentyFourBitAddressing` from the System, 8 MB, ROM window and aliases, maximum host block,
  direct-only assumptions removed from the port. Exit: the ROM reset runs without unexpected dummy-bank accesses.
- **D. Low video** — fixed guest frame buffer, aperture filtering, 640×480×8 default, 800×600×8 and 1024×768×8, palette,
  depths, mode change, full redraw.
- **E. Third merged engine** — objects, partial link, `banks__`, entry point, exits, clean rules, 8 MB reservation,
  image and heap measured on QEMU, Pi 3, 4, 5.
- **F. Automatic selection** — banks only on `System6`; information strings through `assets/strings.tsv`; an unknown
  volume never silently 24-bit.
- **G. Validation** — 6.0.8 to the Finder (IIci, 8 MB, 68030 + FPU, 640×480×8); restarts crossing 6 → 7 → 6; 7.1, 7.6,
  8 and SheepShaver unchanged; no allocation in hot paths; `run-test.sh` on a copy of the System 6 volume.

**Memory tests (24-bit)**: `$00123456`/`$AB123456` same RAM byte; `$00800000`/`$40800000` same ROM byte;
`$00E00000`/`$7FE00000` same frame buffer byte; ROM writes refused or ignored per the ROM bank; holes reach
`dummy_bank`, never arbitrary host memory; byte/word/long big-endian; `xlateaddr` lands in the right buffer. Replay with
`TwentyFourBitAddressing == false`: differing high bytes must no longer alias.

**Measurements per build**: k opcodes/s at boot and at the Finder for 7.1 direct and 6.0.8 banks; compositor at rest and
active; `text`/`data`/`bss`, image size, `_end`; free heap before and after the Mac block and after drivers. The direct
engine must pay only static space, not time per opcode.

## Risks

1. The `boot` filter must be checked on other System 6 installations.
2. Upstream still lists "Add support for System 6.0.x" (`BasiliskII/TODO:7`); the IIci path avoids Classic special cases
   but proves no boot.
3. `$408xxxxx` ROM aliases must reach the low window despite the 32-bit-clean ROM.
4. System 6 must accept the synthetic slot ROM and video driver at a low base: Slot Manager discovery and the aperture
   are the question, not resolutions.
5. Sound, ADB, network and ExtFS come after the first Finder; their absence is not a banks failure.
6. Repeated cross-engine restarts: Mac block, persistent video buffers, renamed statics.
7. The 8 MB reservation must be rebuilt per target and checked on 1 GB boards.

## Not required

A manual 24/32-bit setting, a per-volume machine profile, a second 68k ROM or Gestalt, another 68030 machine, 512×342
video, 14 MB presented as RAM, switching engines under a running CPU, System 7 on banks by default, porting Mini vMac or
Snow to Circle, a JIT for banks, any change to the disk write path.

## References

- `BasiliskII/TECH:43-72`; `Unix/configure.ac:1457-1507`, `:1691-1695`; `main.cpp:74-97`, `:173`;
  `rom_patches.cpp:831-842`; `uae_cpu/memory.h:26-118`, `memory.cpp:37-641`; `uae_cpu_2021/memory.cpp`, `memory.h:58-140`;
  `TODO:7`
- [Inside Macintosh — Addressing Modes and the Virtual Memory Manager](https://dev.os9.ca/techpubs/mac/Memory/Memory-152.html#MARKER-9-47)
- [Apple — Macintosh IIci: Technical Specifications](https://support.apple.com/en-ng/112191)
- [Macintosh IIci Developer Note](https://www.macdat.net/files/pdf/apple/developer_notes/macintosh_iici.pdf)
- [Snow](https://github.com/twvd/snow), [Mini vMac](https://www.gryphel.com/c/minivmac/) — behaviour references for
  System 6, not needed on this path

# Decisions

> What is settled. Each entry gives the decision, why, the evidence, and what would reopen it. Do not
> reopen one without new evidence of that kind.

## Platform

### Circle through circle-stdlib, `STDLIB_SUPPORT=3`

- **Decision**: bare metal on Circle, with newlib and libstdc++ from circle-stdlib.
- **Why**: the Basilisk and SheepShaver cores need a C library and C++ runtime; circle-stdlib provides
  `open`/`read`/`write`/`lseek`, `opendir`/`readdir` over FatFs, sockets, `select`, `clock_gettime`.
- **Evidence**: 24 of 28 core files compiled unchanged on the first probe (2026-08-26); the four exceptions
  were an SDL-only file, a missing include and `ioctl` on an unused path. C++ exceptions work under Circle
  (`tests/smoke/exctest.cpp`).
- **Consequence**: Circle is pinned *through* circle-stdlib, not directly.
- **Would reopen**: a feature Circle cannot provide and circle-stdlib blocks.

### One kernel image carries both Macintosh

- **Decision**: Basilisk II and SheepShaver are linked into one image. Each engine is partial-linked, every
  symbol the PowerPC half defines is renamed (`ppc__` prefix), and switching engines is a function call.
- **Why**: both cores export `InitAll`, `ExitAll`, `PatchROM`, `Execute68k`; renaming on the *objects*
  touches nothing in `external/`. The chain boot it replaced read a second 4 MB image and reset the board.
- **Evidence**: spike of 2026-09-07 — image 2,799 KB, 1,034 symbols renamed, both engines boot to the Finder
  at the same speed as their single images, zero dispatch table needed (the shared code references no
  engine symbol but `memcmp`).
- **Would reopen**: an upstream change that makes the rename list unmanageable.

### `KERNEL_MAX_SIZE` of 4 MB stands

- **Why**: it is circle-stdlib's default hole between the load address and the stacks, not a hardware
  limit (`memorymap.h:47`); the merged image measures about 2,880 KB.
- **Would reopen**: an image approaching 4 MB — raising it is a flag and a library rebuild.

## Emulation

### 256 MB of Mac RAM

- **Why**: the native ceiling of a Quadra 900/950; Basilisk accepts up to 1,023 MB. Everything else must
  fit a 1 GB board.
- **Would reopen**: a community need; lifting the limit is left to whoever needs it.

### `DIRECT_ADDRESSING` on the 68k engine

- **Why**: a Mac address is a host address minus one offset (`MEMBaseDiff`), no MMU dependency, no fixed
  placement.
- **Consequence**: a stray guest address lands in host memory — see [Storage](../topics/storage.md) and
  [68k engine](../topics/engine-68k.md).

### `uae_cpu_2021`, not `uae_cpu`

- **Why**: it is the core macemu builds on AArch64.

### FPU: `fpu_ieee` in IEEE binary128

- **Decision**: the 68881 register is held in AArch64's `long double`, which *is* IEEE binary128 — a
  15-bit exponent with the Macintosh's own bias of 16383 and 112 fraction bits for its 63.
- **Why not `fpu_uae`**: it keeps the register in a `double` (`uae_cpu_2021/fpu/types.h:67`): eleven
  mantissa bits dropped, exponent clamped from 1e4932 to 1e308.
- **Why not MPFR** (upstream's choice on ARM): a `malloc` and a `free` per floating-point instruction.
- **Evidence**: upstream had disabled the quad path because `make_extended()` was not the inverse of
  `extract_extended()` — 1.0 went in, 1.5 came out. `patches/macemu/0008` fixes it, plus
  `make_nan()`/`make_inf()` and denormals. Cost: +62 KB, 3.5% on a System 7.1 boot under QEMU.
- **Verdict instrument**: `fpu_selftest_extended()`, never a boot — System 7.1 reaches the Finder in four
  FPU instructions.

### No JIT

- **Why**: Circle sets `PXN=1` past `_etext` (`lib/translationtable64.cpp`), so the heap is not executable;
  the interpreter already outruns a Quadra. SheepShaver's interpreter needs no executable heap (its decode
  cache is data).
- **Would reopen**: measurements showing the interpreter is the limit for a target System — see
  [68k JIT study](../notes/research/jit-68k.md).

### ROM: the 1993 Quadra/Centris ROM for the 68k engine

- **Decision**: `F1ACAD13`, the 1 MB 32-bit-clean ROM of the Quadra and Centris 610, 650 and 800 — the one
  the Basilisk II community recommends.
- **Evidence**: Basilisk accepts a ROM only if the big-endian word at offset 8 is `0x067C`
  (`rom_patches.cpp:838`). The file also carries model name strings for other Quadras (such as
  "Quadra950"); that is a universal ROM's table, not its identity.
- **The ROM supplies code, not identity**: see `modelid` in [68k engine](../topics/engine-68k.md).

### PowerPC ROMs: Old World TNT or New World

- **Decision**: a 4 MB Old World ROM (such as `powermac9600v1.rom`, TNT) for Mac OS 7.5.2 → 9.0.4, or a
  New World `<CHRP-BOOT>` file for 8.1 → 9.0.4. Checked by `scripts/check-rom.py`.

## Display

### A fixed output frame buffer plus a software compositor

- **Decision**: the output frame buffer is claimed once at the display's native mode and never renegotiated.
  The Mac draws into its own buffer; the compositor converts, scales by an integer and centres.
- **Why**: no HDMI renegotiation or flicker on a mode change, identical behaviour on Pi 3, 4 and 5 (the Pi 5
  cannot set a resolution from the application), and upstream's pixel converters are reused.
- **Evidence**: see [Display](../topics/display.md).

## Storage and data

### No write-back cache between the guest and the card

- **Why**: data safety. Every write the guest issues goes straight to the card, like BlueSCSI, whose
  durability comes from having no cache at all.
- **Evidence**: `--wrap=Sys_write` counts 39 writes to reach the Finder, every one complete.
- **Would reopen**: never without a flush policy and a crash test in the same change.

### Repair a dirty volume at boot with libhfs; never mark it clean ourselves

- **Why**: a volume left "in use" is refused at boot by Mac OS (`badMDBErr`), not damaged. Mounting it
  read-write with libhfs runs the HFS scavenge and remounts it clean. Setting the flag by hand would hide
  real corruption.

## Configuration

### Preferences stay in Basilisk II's own format, in one file

- **Decision**: `BasiliskII_Prefs` at the card root, parsed by upstream's parser, engine-agnostic: `rom` for
  the 68k engine, `romppc` for the PowerPC one, one `engine` line per universal volume.
- **Why not TOML**: the C TOML libraries only read, and the boot menu writes; upstream's format already has
  multi-valued keys (`prefs.cpp:405-413`).
- **Rule**: `SavePrefs()` preserves lines it does not recognise, so one engine never erases the other's keys.

### The two Macintosh keep separate parameter RAM files

- **Why**: SheepShaver's XPRAM is 8,192 bytes, Basilisk's 256, and fields sit at different offsets; one file
  would let each machine silently boot with the other's settings.

### The firmware is its own toolkit, not LVGL

- **Why**: LVGL's Circle glue asserts a 16-bit colour depth, wires no keyboard, uses the cooked mouse, and
  its monochrome theme targets e-paper. A surface-based toolkit renders on the host byte for byte like on
  the board, which is what makes the screens testable. See [Boot menu](../topics/boot-menu.md).

## Engineering

### Changes to upstream are patches, not a fork

- **Why**: a patch that stops applying is an immediate, readable signal; a fork silently accumulates rebase
  debt. See [Building](../contributing/build.md#patches).

### Licence: GPLv3

- **Why**: Circle is GPL-3.0; Basilisk II and SheepShaver are GPLv2-or-later; the combined work can only be
  distributed under GPLv3.

## Goals that are not decisions yet

- **Multicore.** The plan was "S1": the emulation loop on a secondary core, core 0 for interrupts and I/O.
  It is **not implemented** — both engines run on core 0 today. It is a goal, measured against the
  bottleneck study, not a settled fact. See [Architecture](architecture.md#multicore).
- **Clock chain NTP → RTC → card.** Only the card floor exists. See
  [Clock and PRAM](../topics/clock-and-pram.md).
- **Network by sharing the Pi's MAC address.** Designed, not written. See [Network](../topics/network.md).

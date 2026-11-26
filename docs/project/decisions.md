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

### `KERNEL_MAX_SIZE` of 16 MB

- **Decision** (2026-11-14): 16 MB, `KERNEL_MAX_SIZE_MB` in `scripts/build-libs.sh`. It was 4 MB, circle-stdlib's
  default, kept for want of a reason to move it.
- **Why**: the hole between the load address and the stacks is not a hardware limit (`memorymap.h:47`) and nothing
  else asks for it; the 68k JIT's tables alone are 4.9 MB of `.bss`, which the check on `_end` counts
  (`sysinit.cpp:340`, a halt before any log exists). The image itself stays near 3.4 MB without the JIT.
- **Cost**: a library rebuild per target, and a Pi 4 card needs its libraries rebuilt (`build-libs.sh pi4`); the stamp
  makes `build-libs.sh` notice.

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

### FPU: the 68881 in 80-bit softfloat

- **Decision** (2026-11-15, replacing binary128): the register is the 68881's own extended value and every
  operation is the 68K softfloat of WinUAE and Previous — FPCR's rounding mode and precision, the 68881's
  exception flags, the 68040 FPSP's algorithms for the transcendentals — in 64-bit integer arithmetic only
  (`fpu_softfloat.cpp`, `patches/macemu/0013`–`0015`).
- **Why it replaced binary128**: binary128 kept more bits than a 68881, so its answers were not a 68881's,
  and it ignored FPCR's mode and precision and set no exception. It also carried defects found when the
  translator made the FPU the slowest part of the machine ([68k engine](../topics/engine-68k.md#pitfalls)).
  And it exists only where `long double` is binary128: on Apple silicon it is a `double`, so the core could
  be neither shared with upstream's main platform nor tested on the development machine.
- **Why not `fpu_uae`**: a `double` register (`uae_cpu_2021/fpu/types.h:67`): eleven mantissa bits dropped,
  exponent clamped from 1e4932 to 1e308.
- **Why not MPFR** (upstream's choice on ARM and in the Xcode project): GMP and MPFR as dependencies and a
  `malloc` and a `free` per floating-point instruction.
- **Evidence**: sixteen answers computed independently in decimal and a digest of 60,000 operations over every
  mode and precision, the same on an Apple M4 with Clang and on AArch64 with GCC (`tests/host/check_fpu`,
  `src/common/fpu_selftest.h`); clean under UBSan and ASan.
- **Verdict instrument**: those checks, never a boot — System 7.1 reaches the Finder in four FPU instructions.
- **Kept**: `OKAPIA_FPU=ieee` builds the binary128 core (`patches/macemu/0008`) for comparison.

### No JIT by default; two experimental ones

- **Why not by default**: the translator has paid on the Pi (below), but what makes it safe to leave on is not all
  proved yet: System 7.1, restarts in a row, the durability test on hardware. The interpreter already outruns a
  Quadra (5.22× a Quadra 650 on CPU, [Benchmarks](../contributing/benchmarks.md)). The original reason, that Circle
  sets `PXN=1` past `_etext`, does not hold: Okapia clears it for one arena (`execmem_circle.cpp`, proved under QEMU
  and on a Pi 4).
- **What exists** (2026-11-14): `OKAPIA_JIT=1` builds the 68k core of `rcarmo/macemu-jit` (`external/macemu-jit`,
  pinned) in place of `uae_cpu_2021`; with `jit true` in the preferences System 7.1 reaches the Finder under QEMU and
  a hard kill is repaired as without it. On a Pi 4 it reached parity with the interpreter at best, so it is kept for
  reference only. See [68k JIT study](../notes/research/jit-68k.md#first-boot-2026-11-14).
- **A second one** (2026-11-15): `OKAPIA_JIT_ARM64=1` builds a translator of our own beside `uae_cpu_2021`
  (`patches/macemu/0012`), with the registers pinned and an exact source compare after every cache flush. On
  macOS it runs a twenty-test benchmark ten times faster than the interpreter; on Circle it boots System 7.5.5
  under QEMU and passes the durability test. On a Pi 4 (2026-11-15 and 16) Speedometer gives 5.4× the
  interpreter's CPU score, 10 to 16× on the integer tests; its FPU instructions are calls into the softfloat core
  from inside the block, and no FPU instruction reaches the interpreter after the boot. See
  [68k engine](../topics/engine-68k.md#the-translator-okapia_jit_arm641-experimental).
- **Would become the default**: a Speedometer gain on the Pi 4 at least 2× the interpreter's CPU score (**met**,
  5.4×), the [durability tests](../contributing/testing-and-debugging.md) green with it on, and a clean Shut Down
  and restart (open: System 7.1, five restarts, `run-test.sh` on the Pi's card).

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

### QuickDraw acceleration set aside

- **Decision** (2026-11-26, the owner): the 68k QuickDraw hooks (`qdaccel`: native `BitBlt` and `RgnBlt` loops, a
  worker on core 2, the boot menu's box) leave this branch for `wip-quickdraw`, with their study, their host test
  and their measurements. The branch is the last state of that work, uncommitted parts included.
- **Why**: the translator already runs QuickDraw's own loops as ARM64 code, so a hook saves less than it did over
  the interpreter, and every call into the host is a journey out of translated code. Speedometer does not see the
  hooks, with or without the translator. Their code and their modes (`sync`, `parallel`, `probe`, `verify`) were a
  maintenance cost on every start path.
- **Evidence for, kept on record**: MacBench on a Pi 4 with the translator, 152.0 s → 132.2 s, from whole-window
  copies (4.8×), scrolling (3.2×) and regions (−31 %); nothing else moves
  ([Benchmarks](../contributing/benchmarks.md#raspberry-pi-4-the-night-of-2026-11-15)).
- **Would reopen**: a real application (the Finder's windows, a game, a scrolling editor) measured faster by a margin
  that matters with the translator on, against what it costs to keep the hooks right at every depth.

### No write-back cache between the guest and the card

- **Why**: data safety. Every write the guest issues goes straight to the card, like BlueSCSI, whose
  durability comes from having no cache at all.
- **Evidence**: `--wrap=Sys_write` counts 39 writes to reach the Finder, every one complete.
- **Would reopen**: never without a flush policy and a crash test in the same change.

### Repair a dirty volume at boot with libhfs; never mark it clean ourselves

- **Why**: a volume left "in use" is not damaged; a kill leaves at most "MDB needs minor repair". Mounting it
  read-write with libhfs runs the HFS scavenge and remounts it clean. Setting the flag by hand would hide real
  corruption.

## Configuration

### Preferences stay in Basilisk II's own format, in one file

- **Decision**: `Okapia_Prefs` at the card root, parsed by upstream's parser, engine-agnostic: `rom` for
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

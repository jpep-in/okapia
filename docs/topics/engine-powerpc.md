# PowerPC engine

> SheepShaver on Circle: what it emulates, how its memory is placed on a Pi, its interpreter, its ROMs, and
> how it lives beside the 68k engine. Main files: `src/kernel/kernel_ppc.cpp`, `src/circle/sheepshaver/`
> (`main_circle.cpp`, `mac_layout.{h,cpp}`, `platform_bits_circle.cpp`, `video_circle.cpp`,
> `cpu_ticks_circle.cpp`, `config.h`), `patches/macemu/0001`, `0002`, `0004`, `0011`,
> `scripts/gen-ppc-exec.sh`, `scripts/check-engine.sh`.

## Using it

- **ROM**, set by `romppc` (falls back to `rom`):

  | ROM | Form | Nanokernel at `0x30d064` | Mac OS |
  |---|---|---|---|
  | `powermac9600v1.rom` | 4 MB raw, checksum `960E4BE9` | `Boot TNT 0.1p` (Old World) | **7.5.2 → 9.0.4** |
  | `macosrom16.rom` | `<CHRP-BOOT>`, 1,900 KB, LZSS | `NewWorld v1.0` | **8.1 → 9.0.4** — refuses anything older |

  `scripts/check-rom.py` recognises both forms, decompresses in memory, names the type and the Mac OS range it
  allows.
- **Systems**: 7.5.2, 7.5.3, 7.5.5, 7.6, 7.6.1, 8.0, 8.1, 8.5, 8.6, 9.0 and 9.0.4 are the versions upstream
  patches (`SheepShaver/src/rsrc_patches.cpp`, `NEWS`). Nothing beyond 9.0.4.
- **Which engine starts a System**: 8.5 and later only SheepShaver; before 7.5.2 only Basilisk; a universal
  System in between, what its `engine` line says — see [Boot menu](boot-menu.md#engines).
- **Restart** is instant when the startup does not change — see [Startup and shutdown](startup-and-shutdown.md).

## How it works

### What SheepShaver emulates

Basilisk *replaces* the Toolbox; SheepShaver *runs the real PowerMac ROM* — its nanokernel and the 68k
emulator inside it — and patches it only where it touches hardware. The ROM is the machine's low-level operating
system, not an interchangeable part. Okapia uses the **emulated** mode (`EMULATED_PPC`), in which upstream's
whole signal apparatus (`sigaltstack`, the `SIGUSR2` trampoline, the host instruction decoder of the `SIGSEGV`
handler, `main_unix.cpp:21-79`) is compiled out.

### Code shared with the 68k engine

In `SheepShaver/src/Unix/`, everything that is a symbolic link into `BasiliskII/src/Unix/` is platform code
shared by both engines. Okapia's matching files serve both unchanged: `prefs_circle.cpp`, `xpram_circle.cpp`,
`timer_circle.cpp`, `audio_circle.cpp`, `input_circle.cpp`, `extfs_sync_circle.cpp`, `hfs_volume_circle.cpp`,
the compositor and the firmware. What is SheepShaver's own is `main`, `video`, `user_strings`, `sysdeps`, the
interrupt flag values (`INTFLAG_*` differ, and SheepShaver needs `TriggerInterrupt()`), and the emul-op hook.

The core compiled for the board **without a single change** (23 core files plus the interpreter): only a
`src/circle/sheepshaver/config.h` was needed, close to Basilisk's — adding `EMULATED_PPC`, `HAVE_FENV_H`,
`NATMEM_OFFSET`, removing `VERSION_MAJOR`/`VERSION_MINOR` (SheepShaver's `version.h` declares them as `const
int`). `scripts/check-engine.sh sheepshaver` re-runs that sweep.

### Memory layout

Under a host OS SheepShaver needs fixed addresses (`vm_acquire_fixed`), which is what makes it painful to port.
On bare metal the block is ours to place. `MacLayoutPlan()` (`mac_layout.cpp`) is the arithmetic, kept apart
from any allocation so the host tests can check it:

| Region | Guest address | Size |
|---|---|---|
| Low Memory | `0x00000000` | banked into a static array |
| RAM | `0x10000000` | `ramsize` |
| ROM area | right after RAM, aligned to 1 MB | 5 MB (`PatchROM()` copies the last megabyte to the 4 MB boundary, `rom_patches.cpp:728`) |
| Interrupt stack | after the ROM area | 64 KB |
| SheepMem | after the stack | 512 KB (`thunks.h:122`) |
| Frame buffer | after SheepMem | 16 MB (2560×1440 in millions of colours) |
| Kernel Data | `0x68ffe000` (fixed by the ROM) | banked; the block must **end** below it |

That is 277.6 MB for 256 MB of Mac RAM, inside the shared block (`ramsize` + 24 MB). Upstream leaves 1 GB
between RAM and ROM because a Unix host must request both separately; here the ROM follows the RAM.

`patches/macemu/0001` adds `VM_PORT_PLACES_GUEST` to `vm.hpp`, beside the two host cases upstream already
writes: `VMBaseDiff` becomes a variable, Low Memory and the Kernel Data are banked into static arrays, and
**every access outside the block** is routed to a 4 KB stray page — a read returns what was last written there,
as an undriven register would. `gStrayCount` counts them and `gStrayFirst` remembers the first address; the tick
logs them when they move. On a complete 7.6 boot both stay at zero. Absorbing is upstream's own behaviour on a
trapping host (`ignoresegv` defaults to true). `patches/macemu/0002` lets a port that does not trap at all
compile the glue, which otherwise stops on `#error "you don't have the capability to skip instruction"`.

### CPU: `kpx_cpu`, interpreted

- **No AArch64 JIT backend** upstream (`configure.ac:1595-1618` enables dyngen only for PowerPC, x86 and MIPS).
- **No executable heap needed**: the interpreter keeps a **decode cache** (`PPC_DECODE_CACHE`,
  `ppc-cpu.cpp:619-700`) of pre-decoded instructions holding member function pointers — data, not code.
- **A generated file**: `ppc-execute-impl.cpp`, produced by `scripts/gen-ppc-exec.sh` (`c++ -E -DGENEXEC
  ppc-decode.cpp | perl genexec.pl`), preprocessed by the *target* compiler so the list matches the build.
- **Per instruction, faster than `uae_cpu`**, measured on the same host: 895–953 M instructions/s on registers
  and 682 M with memory access, against 486 and 457 M for the 68k core — the decode cache, flattered by a short
  loop. Memory costs the PowerPC 24% against 6% for the 68k: `vm.hpp:27` does not declare
  `VM_CAN_ACCESS_UNALIGNED` for AArch64 (true for Normal memory, not for Device memory), so every 32-bit read is
  four byte reads.
- **How much 68k Mac OS 8.6 really runs**: its System file carries 5.1 MB of data fork in 70 PEF containers —
  native — against about 1.5 MB of 68k code in the resource fork (`gpch` 627 KB the largest). The ROM also has a
  68k dynamic recompiler, which SheepShaver exposes as `jit68k` — **off by default** (`prefs_items.cpp:108`), its
  cache allocation under `#if 0` (`main_unix.cpp:1022-1043`).

### Periodic seam

kpx_cpu never returns while the Mac runs, and upstream always had a thread beside it. `patches/macemu/0004`
adds `powerpc_check_ticks()`, called every `ppc_check_ticks_quantum` instructions (starting at
`PPC_CHECK_TICKS = 50000`); `sheepshaver/cpu_ticks_circle.cpp` recalibrates the quantum to 1 kHz (~950/s
measured), drains input, polls the PRAM every 64 visits (SheepShaver's ROM has no parameter-RAM opcode to hook),
and every five seconds watches the clock, reports and flushes the log.

### Video

SheepShaver runs the Mac's own native video driver and asks the platform for a `VModes[]` table and a frame buffer
at a Mac address. The shared layer ([Display](display.md)) decides which modes fit; this engine names the sizes
with Apple display-mode identifiers — the ten upstream names keep their numbers, the others are numbered past
`APPLE_CUSTOM`, which `patches/macemu/0011` lets the driver list (up to `0xff`, as Basilisk's driver does), with
`VModes` grown to 128 entries. `video_set_dirty_area()` announces what QuickDraw acceleration changed
(`gfxaccel.cpp`, a per-row `memmove` done by the host), which the compositor redraws without comparing.

`VideoVBL()` must end with `VSLDoInterruptService()`, like every upstream platform
(`video_x.cpp:2192`): the Cursor Device Manager's work is queued behind the driver's VBL service, and without it
the pointer position reached the cursor device and never moved on screen.

### Parameter RAM

`/SheepShaver_XPRAM`, 8,192 bytes, separate from Basilisk's 256-byte file: fields sit at different offsets, and
one file would let each Macintosh silently boot with the other's settings. `xpram_circle.cpp` reads one byte more
than it wants so a *longer* file is refused rather than accepted as a short read.

### Mouse

This engine hands the Mac an absolute position through `CursorDeviceDispatch` selector 1 (`MoveTo`), which
does not accelerate. Okapia applies the ROM's own acceleration curve itself and reads the Mouse control panel
setting from `SPVolCtl` — see [Input](input.md).

## Status

- [x] Interpreter compiled and verified outside Circle on AArch64 (2026-09-04)
- [x] Core compiled for the board unchanged; `ENGINE` in the Makefile (2026-09-05)
- [x] Memory plan, checked on the host; `SheepMem`, ROM decode, banking and stray page (2026-09-05)
- [x] Mac OS 7.6 to the Finder with the TNT ROM (2026-09-05)
- [x] One image with the 68k engine, switch by call (2026-09-07)
- [x] Compositor shared; video modes, depths and frameskip at the 68k engine's level (2026-09-07)
- [x] Periodic seam paced by the clock; PRAM written; input drained from the emulation thread (2026-09-09)
- [x] Mouse control panel honoured through the ROM's curve (2026-09-09)
- [x] Sound through the shared audio layer; heard on a Pi 4 (2026-09-13)
- [x] Same resolution list as the 68k engine, 16 MB frame buffer (2026-09-14)
- [x] Restart in place (2026-09-14)
- [ ] Measure and apply `VM_CAN_ACCESS_UNALIGNED` for AArch64 Normal memory (an upstream candidate)
- [ ] Verify QuickDraw acceleration engages (`gfxaccel.cpp` is linked)
- [ ] **Milestone: Mac OS 8.6 installs from its CD onto an empty volume** (`scripts/run-cd.sh`), then the Mac OS 9 Finder
- [ ] A bottleneck study for `kpx_cpu` on the Pi 4, as done for the 68k engine; `Graf` is at 63% of a
      Power Mac 6100/60, and `Disk` fell from 4.72 to 3.08 between two runs (to reproduce first)
- [ ] Evaluate `jit68k`, the ROM's own 68k recompiler
- [ ] Boot-finished signal; a boot from a New World ROM file recorded
- [ ] A second entry into the engine without a board reset (see [Startup and shutdown](startup-and-shutdown.md))

## Pitfalls

- **`SheepMem::Init()` before loading the ROM and before `InitAll()`.** `ThunksInit()` inside `InitAll()` allocates
  in that area; with `base` left at zero the native thunks the ROM calls went into Low Memory silently, and the
  nanokernel lost itself three instructions later. Upstream calls it three lines before loading the ROM
  (`main_unix.cpp:1142`).
- **An interpreter's decode cache goes stale exactly like an icache.** `MakeExecutable()` looked like nothing to
  do; kpx_cpu keys decoded instructions by address, so code loaded over addresses decoded before runs as its
  predecessor. Upstream flushes (`main_unix.cpp:1473`), ROM excepted. Symptom, read backwards: the Mac idles
  forever on the question-mark floppy (no code loaded) and falls apart fifteen seconds into starting a System
  (nothing but code loading).
- **Globals set in the same breath as `VMBaseDiff`**: `Mac2HostAddr()` consults the bounds, so between the two a
  guest address translates to the stray page — setting them a few lines later put `ROMBaseHost` inside a 4 KB
  array and `DecodeROM` wrote 4 MB into it.
- **`ExitAll()` hangs after `exit_emul_ppc()`** — see [Startup and shutdown](startup-and-shutdown.md).
- **A symbol the PowerPC half references cannot be defined in the shared half** — see
  [Architecture](../project/architecture.md#what-exists-once-and-what-exists-twice).
- **The Basilisk ROM check does not apply**: the `0x067C` word and the leading checksum are 68k properties;
  PowerPC ROMs are identified by the nanokernel string (`rom_patches.cpp:682-694`), six accepted.
- **`set -o pipefail` with `strings f | grep -q pattern`** answers "no" when the pattern is there: `grep -q` exits
  early, `strings` dies of SIGPIPE, and that is the pipeline's status. Use `grep -qa` on the file.

## Development notes

- **2026-09-04** — Integration study. The fixed-address obstacle dissolves on bare metal; the interpreter is the
  only real risk. `test-powerpc.cpp` cannot be used as-is: it compares against a results file produced on a real
  PowerPC, behind a dead wiki link, and does not compile interpreter-only.
- **2026-09-05** — Third parties already measured this configuration (kanjitalk755 fork, interpreted, Mac OS
  9.0.4, Speedometer 4, Quadra 605 = 1.00): Pi 4 overall 4.32, Pi 5 8.86, against 14.3 for an x86 desktop with
  JIT. The "gate" of Phase 19 became a confirmation. Reported by others under Linux: cite, never present as ours.
- **2026-09-05** — The boot stopped with the timebase read counter at 3: a stop, not slowness — `SheepMem::Init()`
  missing. Then the 7.6 boot fell apart on a `bctr` with `CTR = 0` in CFM glue: the stale decode cache.
- **2026-09-07** — Chain boot replaced by the merged image.
- **2026-09-13** — Pi 4 Speedometer after the first performance changes: benchmark average 5.17 → 11.08, CPU
  1.24 → 2.96 (95% of a Power Mac 6100/60).

## References

- `SheepShaver/src/rom_patches.cpp:148-199`, `:674-694`, `:728`; `emul_op.cpp:286`, `:426-434`;
  `kpx_cpu/src/cpu/vm.hpp:27`, `:192-218`; `kpx_cpu/src/cpu/ppc/ppc-cpu.cpp:619-700`;
  `Unix/main_unix.cpp:183-189`, `:1011-1140`, `:1473`; `Unix/configure.ac:1580-1618`
- [infinite-mac study](../notes/research/infinite-mac.md), [PowerPC mouse note](../notes/dev/2026-09-09-powerpc-mouse.md)

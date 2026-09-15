# 68k engine

> Basilisk II on Circle: the CPU and FPU cores, the ROM and the model it announces, the core's build, and
> what it costs. Main files: `src/kernel/kernel.cpp`, `src/circle/main_circle.cpp`,
> `src/circle/cpu_ticks_circle.cpp`, `src/circle/emul_op_hook_circle.cpp`, `src/circle/config.h`,
> `src/circle/compat/`, `patches/macemu/0003`, `0005`–`0010`, `scripts/gen-cpu.sh`, `scripts/check-rom.py`.

## Using it

- **ROM**: a 1 MB 32-bit-clean ROM, set by `rom`. The reference is `F1ACAD13`, the 1993 ROM of the
  Quadra/Centris 610, 650 and 800. Check it with `scripts/check-rom.py`.
- **Systems**: System 7.0.1 to Mac OS 8.1 (the range Basilisk's resource patches name); System 7.0 and 7.1
  work with the right `modelid`.
- **Model**: leave `modelidauto true` — Okapia reads the System on the startup volume and picks the model.
- **CPU and FPU**: `cpu 4` (68040) and `fpu true`.

## How it works

### CPU: `uae_cpu_2021`, interpreted

The core macemu builds on AArch64. `scripts/gen-cpu.sh` runs the two-stage generator: `table68k` → `build68k`
(built **for the host**) → `cpudefs.cpp` → `gencpu` (host) → `cpuemu.cpp` in eight parts, `cpustbl.cpp`,
`cpufunctbl.cpp`, `cputbl.h` — about 195,000 generated lines. Size budget measured: the interpreter ~310 KB
of text, the dispatch table 512 KB of data (65,536 pointers), the whole 68k core ~861 KB. Object files are
several times larger because Circle compiles with `-g`; `text`/`data` are what count.

Dispatch is a 524,288-byte table with a `blr`/`ret` per instruction; there are 1,551 distinct `op_xxxx`
handlers (205,904 bytes). Flags are generic (20 bytes in `regflags`).

**Exceptions** use C++ exceptions, upstream's default path. Circle builds with `-fno-exceptions`, so the core
adds `-fexceptions` per file. `EXCEPTIONS_VIA_LONGJMP` exists in the core but no upstream platform enables it,
so nothing tests it and nothing defines its macros — prefer the path upstream actually walks.

### Periodic seam

`cpu_do_check_ticks()` fires when `emulated_ticks` wraps. The quantum is recalibrated against the clock every
64 visits to hold 1 kHz (measured 209 to 1,076 visits per five seconds before, 999/s after). The seam drains
input ([Input](input.md)), and every five seconds watches the ARM clock, prints the `perfreport` counters and
flushes the log to the card.

### Memory

`DIRECT_ADDRESSING`: host = Mac address + `MEMBaseDiff`, set once in `MacMemoryAllocate()`. The globals
(`RAMBaseHost`, `ROMBaseHost`, `MEMBaseDiff`…) belong to `basilisk_glue.cpp`; the platform fills them and
never defines them. Mac RAM sits at Mac address 0 and the ROM at `0x10000000`. Hardware base addresses are
redirected to scratch memory by the ROM patches — **except the ASC's** (`rom_patches.cpp:1053`: "Fake
address only if this is not the ASC base"), so a guest write to the sound chip's address lands at its Mac
address plus `MEMBaseDiff` — on a 4 GB Pi 4, in the Pi's own RAM.

### ROM

Basilisk accepts a ROM only if the big-endian word at offset 8 is `0x067C`, the 32-bit-clean marker required
by `DIRECT_ADDRESSING` (`rom_patches.cpp:838`). The checksum is the leading long, the sum of every 16-bit
word from offset 4. `ROMSize` is only known once the ROM is loaded, which is why the boot menu measures the
file instead.

Two 1 MB ROMs were checked good; despite similar names they come from different lines: `F1ACAD13`
(retained) and `F1A6F343` (the Centris 610 line).

### Model ID

**The machine's identity does not come from the ROM and must not be deduced from it.** `modelid` is written
into the loaded ROM's `productKind` (`rom_patches.cpp:1036`) as "Gestalt model ID minus 6", and Basilisk
disables the NuBus slots in the same `UniversalInfo`: the emulated machine is a synthetic one with a chosen
label.

| `modelid` | Announced | For |
|---|---|---|
| `5` | Mac IIci | System 7.x before Mac OS 8.0 |
| `14` | Quadra 900 | Mac OS 8.x |

Verified on 2026-08-29: with `14`, System 7.1 with its System Enabler 040 stops on an empty "Welcome to
Macintosh" box (the enabler checks the machine); with `5` it reaches the Finder; 7.6 boots with either.

`CKernel::ApplyModelId()` (`kernel.cpp:213`) reads the startup volume's System version before `InitAll()`,
because `rom_patches.cpp` reads `modelid` while patching: `HfsSystemVersion()` walks the catalogue to the
System file of the blessed folder — **found by type and creator (`zsys`/`MACS`), never by name**, since a
French System is called `Système` — and decodes its `vers` resource read-only. The boundary is 8.0.
`modelidauto false` forces the file's value; a failed detection is logged and falls back to it.

### Performance work (Pi 4)

The first Pi 4 Speedometer run (2026-09-12) was far below expectations; seven changes were applied together
(2026-09-13), then a pc-relative fetch:

| Change | Where |
|---|---|
| ARM clock raised from the firmware's 600 MHz to its maximum (`CCPUThrottle`); throttling watched | `hal_circle.cpp` |
| libhfs's byte-by-byte `memcmp` removed from the link, which had replaced the optimised one everywhere | `src/kernel/Makefile` |
| newlib built with `-O2` (circle-stdlib's release flags omitted it) | `patches/circle-stdlib/0001` |
| Card formatted with 32 KB clusters | [Getting started](../getting-started.md) |
| Periodic serial reports behind `perfreport` (a polled PL011 character costs ~87 µs) | seams |
| Dead `fault_pc` store removed (`M68K_NO_FAULT_PC`), `SPCFLAGS` set and cleared without a lock | `patches/macemu/0009` |
| Instruction fetch through `regs.pc_p`, as `uae_cpu` did, instead of recomputing the PC | `patches/macemu/0010` |

Result (Speedometer 4, Pi 4, before → after the first seven): CPU 2.25 → 6.84, benchmark average 2.38 → 7.15,
FPU 1.12 → 2.98, colour 1.92 → 5.58 — 5.2× a Quadra 650 on CPU. See [Benchmarks](../contributing/benchmarks.md)
and the [bottleneck study](../notes/research/bottlenecks.md) for the remaining ranks.

### FPU: binary128

See [Decisions](../project/decisions.md#fpu-fpu_ieee-in-ieee-binary128). `patches/macemu/0008` enables the quad
path and fixes what upstream disabled it for. `fpu_selftest_extended()` runs at startup under
`OKAPIA_TRACE`; `trace_fpu_circle.cpp` wraps `fpuop_arithmetic` to count FPU instructions.

### Build glue

- A hand-written **`config.h`** replaces autoconf's: it **declares and never includes** — pulled in ahead of
  everything, an `#include <arpa/inet.h>` there broke the include order of 39 core files.
- An Okapia **`sysdeps.h`** like every upstream platform's; `-std=gnu++17`, not `c++17` (which defines
  `__STRICT_ANSI__` and hides `strdup` in newlib).
- `src/circle/compat/`: `utime` (through `f_utime`), `<sys/ioctl.h>` and resolver stubs for `ether.cpp`'s UDP
  tunnel, a path Okapia never takes but which is chosen at run time and must compile. Stubs log their call.
- `okapia_circle.h` defines `ASSERT_STATIC` then includes Circle: `circle/types.h` expects it from Circle's
  `assert.h`, and newlib's wins the include search. `circle/new.h` conflicts with `<new>`; allocate raw blocks
  with `CMemorySystem::HeapAllocate`.

## Status

- [x] Core compiled and linked against circle-stdlib, generated tables for AArch64 (2026-08-26)
- [x] ROM loaded and checked, patches applied, 68040 running (2026-08-26)
- [x] Finder reached under QEMU (2026-08-26) and on a Pi 4 (2026-09-12)
- [x] `modelid` from the installed System (2026-08-29)
- [x] FPU in binary128 with a self-test (2026-09-08)
- [x] Seam recalibrated to 1 kHz (2026-09-08)
- [x] Bottleneck ranks 1–7 and the `pc_p` fetch (2026-09-13)
- [ ] Log the initial PC and SP at 68040 initialisation
- [ ] Remaining bottleneck ranks: FatFs fast seek, PMU counters, AArch64 host flags from `macemu-jit`, S1,
      tail-call dispatch — see the [study](../notes/research/bottlenecks.md)
- [ ] Understand the System 7.1 crash with odd sound from the jack (2026-09-14): a stray guest write through
      the unredirected ASC address is the leading hypothesis, unconfirmed
- [ ] System 6 in 24-bit mode as a third engine configuration — see the [study](../notes/research/system6-24bit.md)
- [ ] Automatic ROM selection, as `modelid` is automatic
- [ ] 68k JIT — out of scope unless measurements demand it ([study](../notes/research/jit-68k.md))

## Pitfalls

- **`uae_cpu_2021`, not `uae_cpu`**: the latter is not what macemu builds on AArch64.
- **`gencpu` and `gencomp` are built for the host** and run during the build.
- **A Macintosh boot proves nothing about the FPU.** System 7.1 reaches the Finder in four FPU instructions,
  which is how upstream's binary128 case sat broken for twenty years with 1.0 coming back as 1.5.
- **`quit_program` stays set** after the interpreter leaves; see
  [Startup and shutdown](startup-and-shutdown.md).
- **`DEPTH` is compiled into `libcircle.a`**, so `-DDEPTH=8` in an application Makefile does nothing.
- **Objects in `src/kernel/emu-<engine>/` do not depend on the Makefile.** After changing a flag or a `#define`,
  `make okapia-clean`: stale objects compiled while Basilisk's `D(bug())` tracing was on flooded the serial
  port and cost 4.8× guest speed. `strings kernel8.img | grep 'EmulOp %04x'` tells in a second whether debug
  tracing is linked in.
- **A stray guest address is not a fault here.** With `DIRECT_ADDRESSING` there is no guard in production;
  `DIRECT_ADDRESSING_GUARD` (`patches/macemu/0007`, trace builds) counts accesses beyond the block and names
  the first one and the first far one. The Mac's frame buffer lives past the block, so the first stray is
  usually a pixel.

## Development notes

- **2026-08-26** — Compile probe: 24 of 28 core files unchanged. Link probe: 163 undefined symbols, 87 from the
  libraries, 76 to write; `extfs_unix.cpp` and `sys_unix.cpp` then covered 39 of them unchanged. Kernel 1.76 MB.
- **2026-08-26** — The Happy Mac froze with only `EmulOp 7129` (IRQ) running: the 60 Hz tick was not yet wired.
  `-display none` changed guest behaviour at the time (question-mark floppy headless, Happy Mac windowed).
- **2026-08-29** — `modelid 14` stops System 7.1 at an empty Welcome box; the model follows the System.
- **2026-09-08** — `fpu_uae` replaced by binary128 after 1.0 came back as 1.5 in upstream's disabled path.
- **2026-09-13** — Pi 4 performance: frequency, libraries and loop taxes before any architectural work; see the
  bottleneck study.

## References

- `BasiliskII/src/rom_patches.cpp:838`, `:1036`, `:1053`, `:1069`; `uae_cpu_2021/newcpu.h:281`, `:329-333`;
  `uae_cpu_2021/fpu/types.h:67`; `Unix/configure.ac:1651`, `:1660`, `:1691`, `:1881-1885`
- [Bottleneck study](../notes/research/bottlenecks.md), [68k JIT study](../notes/research/jit-68k.md),
  [System 6 study](../notes/research/system6-24bit.md)

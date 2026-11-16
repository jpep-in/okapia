# 68k engine

> Basilisk II on Circle: the CPU and FPU cores, the ROM and the model it announces, the core's build, and
> what it costs. Main files: `src/kernel/kernel.cpp`, `src/circle/main_circle.cpp`,
> `src/circle/cpu_ticks_circle.cpp`, `src/circle/emul_op_hook_circle.cpp`, `src/circle/config.h`,
> `src/circle/compat/`, `src/circle/fpu_selftest.h`, `patches/macemu/0003`, `0005`–`0010`, `0013`–`0015`,
> `scripts/gen-cpu.sh`, `scripts/check-rom.py`.

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

### FPU: the 68881 in 80-bit softfloat

See [Decisions](../project/decisions.md#fpu-the-68881-in-80-bit-softfloat). `fpu_softfloat.cpp`
(`patches/macemu/0014`) is upstream's `fpu_mpfr.cpp` with MPFR replaced by the 68K softfloat of WinUAE and
Previous (`0013`, `uae_cpu_2021/fpu/softfloat/`, provenance in its `README`):

- **The register is the 68881's own value**: sign, 15-bit exponent, 64-bit significand with its integer bit,
  laid out as softfloat's `floatx80` (`fpu/types.h`).
- **Every result is rounded as FPCR says** (mode and precision), and softfloat's flags become the exception
  byte, the accrued byte following. The 68040's `FS…`/`FD…` forms round to single or double regardless.
- **The transcendentals are the 68040 FPSP's algorithms**, the ones a Quadra runs from its ROM; packed decimal
  goes through softfloat's conversions; `FMOVECR` returns the constant ROM as a 68881 does, its two truncated
  entries included.
- **Only 64-bit integers**: no `long double`, no host floating-point state, no allocation. The same bits on
  every host — the digest below is identical on Apple silicon (Clang) and AArch64 (GCC).
- **A fast path for the common case** (`fpu/softfloat_fast.h`): `FADD`, `FSUB` and `FMUL` of two normal values
  at extended precision rounded to nearest, in one 128-bit integer, softfloat's result bit for bit; and
  softfloat's `mul64To128()` as `mul` + `umulh`.
- **Not done**: exceptions enabled in FPCR are recorded, not taken (as in `fpu_mpfr.cpp`); `FMOVECR`'s
  undefined offsets read zero.

`OKAPIA_FPU=ieee` builds the binary128 core of `patches/macemu/0008` instead, to compare against; the
`macemu-jit` route (`OKAPIA_JIT=1`) has only that one. Under `OKAPIA_TRACE`, `kernel.cpp` runs
`src/circle/fpu_selftest.h` before the Macintosh starts: sixteen answers computed independently, then the
digest of 60,000 operations over every mode and precision, compared with the constant `tests/host/check_fpu`
pins on the development machine, and the fast paths against softfloat over 600,000 operations. `trace_fpu_circle.cpp` counts and times the instructions.

### The translator (`OKAPIA_JIT_ARM64=1`, experimental)

`patches/macemu/0012` adds a 68k to AArch64 translator beside the interpreter, in `uae_cpu_2021/jit_arm64/`.
It is compiled with `make -C src/kernel OKAPIA_JIT_ARM64=1` (after `okapia-clean`) and used when the card says
`jit true`; the interpreter, its tables and the binary128 FPU are the ones described above, and are what runs
every instruction the translator does not know.

- **Registers**: D0–D7 in `w19`–`w26`, A0–A7 in `w8`–`w15`, for as long as translated code runs. Condition
  codes are the host's NZCV; X is in `w17`.
- **Blocks** branch to each other directly; returns and indirect jumps go through a table of 65,536 entries.
  Every backward transfer spends a budget kept in `w16`, so the execution loop is reached every few thousand
  of them: that is where interrupts are seen and where `jit_arm64_host_poll()` calls the seam by the clock.
- **A-line traps** are the 68040's exception written out in the block, in supervisor state with no trace.
- **What it does not know leaves the block**: the instruction is run by the interpreter's handler, called from
  translated code, and the block ends there. An emulator opcode is one of those, so every `EmulOp` is a
  journey: the sixteen registers and the flags written out, the handler, everything read back, and a jump
  through the return table. `MOVE CCR,<ea>` and `MOVE <ea>,CCR` are translated for that reason alone — the
  QuickDraw trampolines open and close with them, and each call used to make that journey three times.
- **Coherence**: a `CPUSHA`, or a `FlushCodeCache()` over translated code, raises an epoch. A block of an
  older epoch leaves at its first instruction and is compared, byte for byte, with its source before it runs
  again; nothing is rewritten in translated code for that, and no checksum stands in for the comparison.
- **Circle's side** is `jit_arm64_circle.cpp`: 16 MB of heap made executable once (`execmem_circle.cpp`),
  claimed after the Mac RAM block, and the 1 kHz seam.

It was developed and tested in upstream Basilisk II on macOS (Apple silicon), where it boots System 7.5.5 and
runs a benchmark of twenty tests in a tenth of the interpreter's time, with identical results.

On Circle (2026-11-15): the kernel builds for QEMU (3,406 KB) and for the Pi 4 (3,455 KB). Under QEMU, System
7.5.5 reaches the Finder with `jit true` in 14 s, the screen is the interpreter's, and
`OKAPIA_EXTRA_PREFS='jit true' ./scripts/run-test.sh 40` gives its usual verdict (killed, scavenged, clean).
On a Pi 4 it was seen running from the serial port only, 160 s into a session with `qdaccel sync` on as well;
its boot lines were not captured and **nothing has been measured there**.

With the translator the `k opcodes` figure of the five-second line is not a speed: the seam is reached by the
clock, so it counts visits times a quantum (about 500 k/s whatever the Macintosh does).

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
- [x] FPU replaced by the 68881 in 80-bit softfloat; same bits on the Mac and under QEMU, System 7.5.5 to the
      Finder (2026-11-15)
- [x] The softfloat FPU on a Pi 4: Speedometer's FPU tests (2026-11-16, see the development notes)
- [ ] The fast paths on a Pi 4: the traced boot's lines, Speedometer's FPU tests, and the time per operation
      from `trace_fpu_circle.cpp`
- [ ] FPU instructions in the translator: called without leaving the block, `FBcc` translated; then, if the
      measurement asks for it, the common operations inline
- [x] Seam recalibrated to 1 kHz (2026-09-08)
- [x] Bottleneck ranks 1–7 and the `pc_p` fetch (2026-09-13)
- [ ] Log the initial PC and SP at 68040 initialisation
- [ ] Remaining bottleneck ranks: FatFs fast seek, PMU counters, AArch64 host flags from `macemu-jit`, S1,
      tail-call dispatch — see the [study](../notes/research/bottlenecks.md)
- [ ] Understand the System 7.1 crash with odd sound from the jack (2026-09-14): a stray guest write through
      the unredirected ASC address is the leading hypothesis, unconfirmed
- [ ] System 6 in 24-bit mode as a third engine configuration — see the [study](../notes/research/system6-24bit.md)
- [ ] Automatic ROM selection, as `modelid` is automatic
- [ ] 68k JIT of `macemu-jit` — experimental behind `OKAPIA_JIT=1`; boots System 7.1 under QEMU, at best parity
      with the interpreter on a Pi 4. Superseded by the translator below, kept for reference
      ([study](../notes/research/jit-68k.md#first-boot-2026-11-14))
- [ ] 68k translator (`OKAPIA_JIT_ARM64=1`, `patches/macemu/0012`) — boots System 7.5.5 under QEMU and passes
      `run-test.sh` with `jit true`; runs on a Pi 4 (2026-11-15). Open, in order: System 7.1 and 7.6.1, five
      restarts and Shut Down, a timed benchmark on the Pi 4 (Speedometer, MacBench), the Mac's tick count
      against the clock under load; then the allocations it makes while the Macintosh runs

## Pitfalls

- **The translator allocates while the Macintosh runs** (`malloc` per block for its copy of the source,
  `std::unordered_map` and `std::vector` for its bookkeeping). That breaks the resource rule of `AGENTS.md`
  and is owed before it can be anything but an experiment: preallocated arenas, reset when everything is
  forgotten.
- **A translator makes 68040-clean code a requirement.** Code modified without a cache flush runs stale until
  the next one, as it would on a real 68040 with a larger cache than any ever built. The interpreter forgives
  it; `jit true` does not.
- **`qdaccel` and the translator are on together** (2026-11-15), and the translator changes what a hook is
  worth: QuickDraw's own loops are ARM64 code now, and the call to the host is what costs. See
  [Display](display.md#68k-quickdraw) for the gate this led to and the measurements.
- **A fault that moves with timing is not yet a fault of the translator.** One drawing check of MacBench
  differed by nine pixels between two configurations, stably, and only with `jit true`. A long run of
  suspicion went to the carry shortcut between blocks; the cause was the Font Manager failing to reallocate a
  scaled Chicago strike in a full system heap and drawing from the 12 point one. Trace the guest (which
  string, which strike handle) before bisecting the translator. Two test modes came out of the hunt and stay:
  `B2_JIT_POISON` enters every block said to ignore incoming flags with those flags inverted, `B2_JIT_IRQ=n`
  raises an interrupt every n returns to the loop (hosted builds; see `jit-test/README.md` upstream).

- **`uae_cpu_2021`, not `uae_cpu`**: the latter is not what macemu builds on AArch64.
- **`gencpu` and `gencomp` are built for the host** and run during the build.
- **A Macintosh boot proves nothing about the FPU.** System 7.1 reaches the Finder in four FPU instructions,
  which is how upstream's binary128 case sat broken for twenty years with 1.0 coming back as 1.5.
- **More bits than a 68881 is not more exact.** The binary128 core kept 113 bits and so gave answers a 68881
  never gives, ignored FPCR's mode and precision, and raised no exception but in `FSCALE`. Reading it for the
  softfloat change found four more defects: `isnan()`/`isinf()` took the `double` branch under
  `USE_QUAD_DOUBLE` (`mathlib.h:363`, `:423`), so every operation called `__trunctfdf2` four or five times and
  any finite value past 1.8e308 counted as infinite; `FINT`, `FINTRZ`, `FMOD`, `FREM` and `FMOVE` to an integer
  went through `(int)x` and `(int)(x + 0.5)` (`mathlib.h:1223-1232`), wrong past 2^31, for negatives and for
  ties; `FMOVE.X` out truncated; and by reading, `FMOVE.D` set a fraction bit worth 2^-49
  (`extract_double()`). The known answers in `fpu_selftest.h` are those cases.
- **An overflow raises OVFL, not OVFL and INEX2.** Motorola's accrued rule ORs `OVFL` into `INEX` for that
  reason; a test written from IEEE 754 expects both and is wrong.
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
- **2026-11-15** — With the translator, FPU 1.3× against 5.4× on CPU. Binary128 replaced by the 68K softfloat
  (patches `0013`–`0015`): the same digest on an Apple M4 and under QEMU's AArch64, clean under UBSan and ASan.
  On the M4, add 8.8 ns, mul 7.2, div 19.5, sqrt 17.8, sin 208, etox 160. Kernel 3,408 KB (the core 12 KB,
  the softfloat 68 KB).
- **2026-11-16** — Speedometer 4 on the Pi 4 with the translator, softfloat against binary128 (one run each):
  FPU average 3.79 against 3.81, but K'Whet +20% (6.09; the FPSP's transcendentals beat newlib's `long double`
  ones) and FFT −15% (2.29), Matrix −18% (3.00): softfloat's add and multiply, written for 32-bit hosts, lost
  to libgcc. Fast path for `FADD`/`FSUB`/`FMUL` and a 128-bit `mul64To128()` added: on the M4 a multiplication
  2.2 ns against 8.3; 179 million operations compared with softfloat, none differing; the same under QEMU's
  AArch64. Not yet measured on the Pi. The iMac G3 the owner compared with (FPU 27.74) runs Speedometer's
  PowerPC code on a hardware double FPU.
- **2026-09-13** — Pi 4 performance: frequency, libraries and loop taxes before any architectural work; see the
  bottleneck study.

## References

- `BasiliskII/src/rom_patches.cpp:838`, `:1036`, `:1053`, `:1069`; `uae_cpu_2021/newcpu.h:281`, `:329-333`;
  `uae_cpu_2021/fpu/types.h:67`; `Unix/configure.ac:1651`, `:1660`, `:1691`, `:1881-1885`
- [Bottleneck study](../notes/research/bottlenecks.md), [68k JIT study](../notes/research/jit-68k.md),
  [System 6 study](../notes/research/system6-24bit.md)
